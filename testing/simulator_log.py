"""Stream one iOS Simulator test launch's Godot log to the AS3 test runner."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def follow(log, process, marker, deadline):
    if not marker:
        raise ValueError("A simulator test needs a completion marker.")
    # Godot creates a fresh log for this launch. Wait for it before opening the file.
    while not log.exists() and process.poll() is None and time.monotonic() < deadline:
        time.sleep(0.1)
    if not log.exists():
        raise ValueError("Simulator test exited or timed out before creating its Godot log.")
    pending = b""
    with log.open("rb") as source:
        while time.monotonic() < deadline:
            exited = process.poll() is not None
            chunk = source.read(65536)
            if chunk:
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
                lines = (pending + chunk).split(b"\n")
                pending = lines.pop()
                if any(line.rstrip(b"\r") == marker for line in lines):
                    return process.returncode or 0
            elif exited:
                if pending.rstrip(b"\r") == marker:
                    return process.returncode or 0
                return process.returncode or 1
            else:
                time.sleep(0.1)
    raise ValueError("Simulator test timed out waiting for its completion marker.")


def run(device, bundle, case):
    simctl = ["xcrun", "simctl"]
    data = subprocess.check_output([*simctl, "get_app_container", device, bundle, "data"], text=True, timeout=30)
    documents = Path(data.strip()) / "Documents"
    documents.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=documents) as tmp:
        log = Path(tmp) / "engine.log"
        command = [*simctl, "launch", "--console", device, bundle, "--log-file", str(log)]
        if case.get("headless", True):
            command.append("--headless")
        process = subprocess.Popen(command, stdout=sys.stderr, env=dict(os.environ, SIMCTL_CHILD_KAS3_TESTS="1"))
        try:
            return follow(log, process, case["marker"].encode(), time.monotonic() + case.get("timeout", 120))
        finally:
            try:
                subprocess.run([*simctl, "terminate", device, bundle], capture_output=True, timeout=15)
            except (OSError, subprocess.TimeoutExpired) as error:
                print(f"Could not stop simulator test app: {error}", file=sys.stderr)
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
