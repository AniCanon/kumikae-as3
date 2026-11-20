"""Collect Godot's test protocol from a fresh headless Chromium profile."""
from urllib.parse import unquote
import os
import re
import shutil
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


def run(web, product, chrome, marker):
    html = product.read_text()
    if not re.search(r"<head\b[^>]*>", html, re.I):
        raise ValueError("The Web test export needs an HTML head element.")
    product.write_text(re.sub(r"<head\b[^>]*>", lambda match: match[0] + CAPTURE, html, count=1, flags=re.I))
    with web.server_for(product) as server, tempfile.TemporaryDirectory(prefix="kas3-chrome-") as profile:
        thread = threading.Thread(target=server.serve_forever)
        thread.start()
        try:
            url = f"http://127.0.0.1:{server.server_port}/{product.name}"
            command = [chrome, "--headless", "--enable-logging=stderr", "--enable-unsafe-swiftshader",
                       "--no-first-run", "--no-default-browser-check", "--user-data-dir=" + profile, url]
            with subprocess.Popen(command, stderr=subprocess.PIPE, text=True, errors="replace") as process:
                try:
                    for line in process.stderr:
                        match = re.search(r'\bKAS3_BROWSER_LOG ([^"]*)"', line)
                        if match:
                            message = unquote(match[1])
                            print(message, flush=True)
                            if marker in message.splitlines():
                                return 0
                    raise ValueError("Chromium exited before the suite completed.")
                finally:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
        finally:
            server.shutdown()
            thread.join()
