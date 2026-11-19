"""Install a built test application, then collect the result written by this launch."""
import argparse
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import time
import uuid


def finished(result, token):
    if result.get("run") != token:
        return False
    if result.get("version") != 1 or result.get("passed") is not True:
        raise ValueError("The simulator suite failed. See the game's test report.")
    checks = result.get("checks")
    if type(checks) is not int or checks < 1:
        raise ValueError("The simulator report has no valid check count.")
    return True


def collect(app, device, evidence, timeout):
    with (app / "Info.plist").open("rb") as stream:
        bundle = plistlib.load(stream).get("CFBundleIdentifier")
    if not bundle:
        raise ValueError("The test application Info.plist has no CFBundleIdentifier.")

    def simctl(*args, check=False, **kwargs):
        result = subprocess.run(["xcrun", "simctl", *args], text=True, capture_output=True, timeout=90, **kwargs)
        if check and result.returncode:
            detail = (result.stderr or result.stdout or f"xcrun simctl {args[0]} failed").strip()
            raise ValueError(detail)
        return result

    simctl("terminate", device, bundle)
    simctl("install", device, str(app), check=True)
    data = Path(simctl("get_app_container", device, bundle, "data", check=True).stdout.strip()) / "Documents"
    token = uuid.uuid4().hex
    started = time.time() - 2.0
    simctl("launch", device, bundle, env=dict(os.environ, SIMCTL_CHILD_KAS3_TEST_RUN=token), check=True)
    report = data / "kas3-test-result.json"
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if report.is_file():
            try:
                loaded = json.loads(report.read_text())
                result = loaded if isinstance(loaded, dict) else {}
            except json.JSONDecodeError:
                result = {}
            if result.get("run") == token:
                evidence = evidence / token
                evidence.mkdir(parents=True, exist_ok=True)
                for path in data.iterdir():
                    if path.is_file() and path.suffix in {".json", ".txt", ".png"} and (
                        path == report or path.stat().st_mtime >= started
                    ):
                        shutil.copy2(path, evidence / path.name)
                if finished(result, token):
                    print(f"Passed {result.get('checks', 0)} integration checks.")
                    return
        time.sleep(0.4)
    raise ValueError("Timed out waiting for this launch's test report.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    parser.add_argument("--device", required=True)
    parser.add_argument("--evidence", required=True, type=Path)
    parser.add_argument("--timeout", type=float, default=300)
    args = parser.parse_args()
    try:
        collect(args.app, args.device, args.evidence, args.timeout)
    except (OSError, ValueError, subprocess.SubprocessError) as failure:
        parser.exit(1, str(failure) + "\n")
