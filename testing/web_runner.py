"""Collect Godot's test protocol from a fresh headless Chromium profile."""
from urllib.parse import unquote
import os
import re
import shutil
import signal
import subprocess
import tempfile
import threading


def browser():
    selected = os.environ.get("KAS3_CHROME")
    candidates = [selected] if selected else [
        "chromium", "chromium-browser", "google-chrome", "chrome",
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    ]
    for name in candidates:
        executable = shutil.which(name)
        if executable:
            return executable
    raise ValueError("Install Chromium or set KAS3_CHROME to its executable to run Web tests.")


# Frame each console message on one line, including blank and multiline test protocol fields.
CAPTURE = """<script>
for (const level of ['log', 'warn', 'error']) {
    const write = console[level].bind(console);
    console[level] = (...args) => {
        write('KAS3_BROWSER_LOG ' + encodeURIComponent(args.join(' ')));
    };
}
</script>"""


def _stop_browser(process):
    if process.poll() is not None:
        return
    if os.name == "posix":
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except (ProcessLookupError, PermissionError):
            pass
    else:
        process.terminate()
    try:
        process.wait(timeout=5)
        return
    except subprocess.TimeoutExpired:
        pass
    if os.name == "posix":
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except (ProcessLookupError, PermissionError):
            pass
    else:
        process.kill()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        pass


def run(web, product, chrome, marker, timeout=120):
    html = product.read_text()
    if not re.search(r"<head\b[^>]*>", html, re.I):
        raise ValueError("The Web test export needs an HTML head element.")
    product.write_text(re.sub(r"<head\b[^>]*>", lambda match: match[0] + CAPTURE, html, count=1, flags=re.I))
    with web.server_for(product) as server, tempfile.TemporaryDirectory(prefix="kas3-chrome-") as profile:
        server.daemon_threads = True
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            url = f"http://127.0.0.1:{server.server_port}/{product.name}"
            command = [chrome, "--headless", "--enable-logging=stderr", "--enable-unsafe-swiftshader",
                       "--no-first-run", "--no-default-browser-check", "--user-data-dir=" + profile, url]
            timed_out = threading.Event()
            process = subprocess.Popen(
                command,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
                text=True,
                errors="replace",
                start_new_session=(os.name == "posix"),
            )

            def on_timeout():
                timed_out.set()
                _stop_browser(process)

            timer = threading.Timer(timeout, on_timeout) if timeout else None
            if timer is not None:
                timer.daemon = True
                timer.start()
            try:
                for line in process.stderr:
                    match = re.search(r'\bKAS3_BROWSER_LOG ([^"]*)"', line)
                    if match:
                        message = unquote(match[1])
                        print(message, flush=True)
                        if marker in message.splitlines():
                            return 0
                if timed_out.is_set():
                    raise ValueError(f"Chromium timed out after {timeout} seconds waiting for the suite.")
                raise ValueError("Chromium exited before the suite completed.")
            finally:
                if timer is not None:
                    timer.cancel()
                _stop_browser(process)
                if process.stderr is not None:
                    process.stderr.close()
        finally:
            server.shutdown()
            thread.join(timeout=5)
