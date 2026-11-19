"""Run the iOS test scene in an installed Simulator app and require native AS3."""
import argparse
import os
import plistlib
from pathlib import Path
import re
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("simulator", help="UDID of a booted test simulator")
    parser.add_argument("app", type=Path, help="the exported test project's .app")
    parser.add_argument("--source-dir", type=Path, help="watch the source folder as a Rider debug run does")
    parser.add_argument("--interpreted", action="store_true", help="require the interpreter for an opted-out preset")
    args = parser.parse_args()
    with (args.app / "Info.plist").open("rb") as stream:
        bundle = plistlib.load(stream)["CFBundleIdentifier"]
    simctl = ["xcrun", "simctl"]
    subprocess.run([*simctl, "install", args.simulator, args.app], check=True)
    data = subprocess.check_output([*simctl, "get_app_container", args.simulator, bundle, "data"], text=True).strip()
    with tempfile.TemporaryDirectory(dir=Path(data, "Documents")) as folder:
        log = Path(folder, "stdout")
        log.touch()
        env = os.environ.copy()
        if args.source_dir:
            env["SIMCTL_CHILD_KAS3_SOURCE_DIR"] = str(args.source_dir.resolve())
        subprocess.run([*simctl, "launch", args.simulator, bundle, "--log-file", log], check=True, env=env)
        try:
            deadline = time.monotonic() + 60
            while time.monotonic() < deadline:
                output = log.read_text(errors="replace")
                if "AS3TEST DONE" in output:
                    break
                time.sleep(0.25)
            print(output)
            native = "ActionScript: the scripts run as native code." in output
            if native == args.interpreted:
                raise SystemExit("The exported game did not use the execution mode selected in the iOS preset.")
            if not re.search(r"AS3TEST DONE passed=[1-9]\d* failed=0\b", output):
                raise SystemExit("The iOS test scene did not finish with all checks passing.")
        finally:
            subprocess.run([*simctl, "terminate", args.simulator, bundle], check=False)


if __name__ == "__main__":
    main()
