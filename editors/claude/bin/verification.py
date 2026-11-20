"""Run the same selected AS3 fixture on each requested target."""
import subprocess
import sys

from device_lease import device_lease
from game import device_for
from game_export import run_export


def verify(kit, project, godot, targets, case=None, preset=None, ios_device=None, android_device=None):
    if not (project / "kas3-tests.json").is_file():
        raise ValueError(f"No kas3-tests.json in {project}")
    results = {}
    for target in targets:
        print(f"Testing {target}", flush=True)
        try:
            device = device_for(target, ios_device if target == "ios-simulator" else android_device)
        except (ValueError, OSError, subprocess.SubprocessError) as error:
            results[target] = f"NOT RUN ({error})"
            continue
        try:
            if target == "desktop":
                import_project(project, godot)
            command = [sys.executable, "-I", kit / "testing/runner.py", project / "kas3-tests.json",
                       "--require-pass", "--compiler", kit / "kas3c", "--godot", godot,
                       "--runtime", "godot" if target == "desktop" else target]
            for flag, value in (("--device", device), ("--case", case), ("--preset", preset)):
                if value:
                    command.extend([flag, value])
            with device_lease(device):
                results[target] = "PASS" if subprocess.run(command).returncode == 0 else "FAIL"
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            results[target] = f"FAIL ({error})"
    for target, result in results.items():
        print(f"{target}: {result}")
    return int(any(result != "PASS" for result in results.values()))


def import_project(project, godot):
    run_export([godot, "--headless", "--path", str(project), "--editor", "--quit-after", "120", "--max-fps", "60"])
