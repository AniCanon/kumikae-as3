"""Export a Godot preset and follow the AS3 game on the chosen Android device."""
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import zipfile


def launch_activity(badging):
    package = re.search(r"^package: name='([^']+)'", badging, re.M)
    activity = re.search(r"^launchable-activity: name='([^']+)'", badging, re.M)
    if not package:
        raise ValueError("Android Build Tools could not identify the game's launch activity.")
    return package[1], activity[1] if activity else None


def run(project, preset, serial, sdk, godot, runtime):
    adb = [str(sdk / "platform-tools/adb"), "-s", serial]
    def shell(*args):
        return subprocess.check_output([*adb, "shell", *args], text=True).strip()
    abi = shell("getprop", "ro.product.cpu.abilist").split(",")
    if "arm64-v8a" not in abi:
        raise ValueError("Select an ARM64 Android device or emulator.")
    versions = [p for p in (sdk / "build-tools").glob("*/aapt") if re.fullmatch(r"\d+(\.\d+)*", p.parent.name)]
    if not versions:
        raise ValueError("Install Android SDK Build Tools to inspect the exported game.")
    aapt = max(versions, key=lambda p: tuple(map(int, p.parent.name.split("."))))
    with tempfile.TemporaryDirectory(prefix="kumikae-android-") as folder:
        stage = Path(folder) / "game"
        excluded = {".godot", ".git", ".idea", ".run", "build", ".build"}
        shutil.copytree(project, stage, ignore=lambda path, names: excluded.intersection(names) if Path(path) == project else [])
        descriptor = stage / "kas3.gdextension"
        text = descriptor.read_text()
        if not re.search(r"(?m)^\s*android(?:\.|\s*=)", text):
            if not runtime.is_file():
                raise ValueError("The Android ARM64 AS3 runtime is missing from this plugin.")
            destination = stage / "bin/android/arm64-v8a" / runtime.name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(runtime, destination)
            text, count = re.subn(r"(?m)^\[libraries\]\s*$", '[libraries]\nandroid.arm64 = "res://' +
                                  destination.relative_to(stage).as_posix() + '"\n', text, count=1)
            if not count:
                raise ValueError("The AS3 extension descriptor needs a libraries section.")
            descriptor.write_text(text)
        apk = Path(folder) / "game.apk"
        env = dict(os.environ, ANDROID_HOME=str(sdk), ANDROID_SDK_ROOT=str(sdk))
        (stage / ".godot").mkdir()
        (stage / ".godot/extension_list.cfg").write_text("res://kas3.gdextension\n")
        subprocess.run([godot, "--headless", "--path", stage, "--editor", "--quit-after", "120", "--max-fps", "60"], env=env, check=True)
        subprocess.run([godot, "--headless", "--path", stage, "--export-debug", preset, apk], env=env, check=True)
        with zipfile.ZipFile(apk) as archive:
            if not any(item.startswith("lib/arm64-v8a/") and "kas3" in item for item in archive.namelist()):
                raise ValueError("The selected Android preset did not export the ARM64 AS3 runtime.")
        package, activity = launch_activity(subprocess.check_output([aapt, "dump", "badging", apk], text=True))
        subprocess.run([*adb, "install", "-r", apk], check=True)
        component = package + "/" + activity if activity else shell(
            "cmd", "package", "resolve-activity", "--brief", "-a", "android.intent.action.MAIN",
            "-c", "android.intent.category.LAUNCHER", package).splitlines()[-1]
        if not component.startswith(package + "/"):
            raise ValueError("Android could not resolve a launcher for " + package)
        result = shell("am", "start", "-W", "-n", component)
        print(result, flush=True)
        if "Status: ok" not in result:
            raise ValueError("Android could not launch this game.")
        try:
            while subprocess.run([*adb, "shell", "pidof", package], stdout=subprocess.DEVNULL).returncode == 0:
                time.sleep(1)
        finally:
            subprocess.run([*adb, "shell", "am", "force-stop", package], check=True)


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(130))
    try:
        run(Path(sys.argv[1]), sys.argv[2], sys.argv[3], Path(sys.argv[4]), sys.argv[5], Path(sys.argv[6]))
    except (OSError, ValueError, subprocess.CalledProcessError) as failure:
        sys.exit(str(failure))
