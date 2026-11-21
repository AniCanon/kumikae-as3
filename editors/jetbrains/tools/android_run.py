"""Export a Godot preset and follow the AS3 game on the chosen Android device."""
import importlib.util
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import time
import zipfile


spec = importlib.util.spec_from_file_location("game_export", Path(__file__).with_name("game_export.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

_PACKAGE_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z][A-Za-z0-9_]*)+$")
_ACTIVITY_RE = re.compile(r"^[A-Za-z0-9_.$]+$")
_COMPONENT_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z][A-Za-z0-9_]*)+/[A-Za-z0-9_.$]+$")


def validate_package(package):
    if not _PACKAGE_RE.fullmatch(package):
        raise ValueError("Invalid Android package name: " + package)
    return package


def validate_component(package, component):
    validate_package(package)
    if not component.startswith(package + "/") or not _COMPONENT_RE.fullmatch(component):
        raise ValueError("Android could not resolve a valid launcher for " + package)
    return component


def shell_args(*args):
    return [shlex.quote(str(arg)) for arg in args]


def launch_activity(badging):
    package = re.search(r"^package: name='([^']+)'", badging, re.M)
    activity = re.search(r"^launchable-activity: name='([^']+)'", badging, re.M)
    if not package:
        raise ValueError("Android Build Tools could not identify the game's launch activity.")
    pkg = validate_package(package[1])
    act = activity[1] if activity else None
    if act is not None and not _ACTIVITY_RE.fullmatch(act):
        raise ValueError("Android Build Tools reported an invalid launch activity.")
    return pkg, act


def wait_for_device(adb):
    try:
        subprocess.run([*adb, "wait-for-device"], check=True, timeout=45)
    except subprocess.TimeoutExpired:
        raise ValueError("The selected Android device did not reconnect. Check that it is running.") from None


def install(adb, apk):
    wait_for_device(adb)
    subprocess.run([*adb, "install", "-r", apk], check=True, timeout=180)


def run(project, preset, serial, sdk, godot, runtime):
    if not serial or serial.startswith("-"):
        raise ValueError("Select a valid Android device serial.")
    adb = [str(sdk / "platform-tools/adb"), "-s", serial]

    def shell(*args, timeout=30):
        return subprocess.check_output([*adb, "shell", *shell_args(*args)], text=True, timeout=timeout).strip()

    wait_for_device(adb)
    abi = shell("getprop", "ro.product.cpu.abilist").split(",")
    if "arm64-v8a" not in abi:
        raise ValueError("Select an ARM64 Android device or emulator.")
    versions = [p for p in (sdk / "build-tools").glob("*/aapt") if re.fullmatch(r"\d+(\.\d+)*", p.parent.name)]
    if not versions:
        raise ValueError("Install Android SDK Build Tools to inspect the exported game.")
    aapt = max(versions, key=lambda p: tuple(map(int, p.parent.name.split("."))))
    with tempfile.TemporaryDirectory(prefix="kumikae-android-") as folder:
        stage = Path(folder) / "game"
        module.stage_game(project, stage, runtime, android=True)
        apk = Path(folder) / "game.apk"
        env = dict(os.environ, ANDROID_HOME=str(sdk), ANDROID_SDK_ROOT=str(sdk))
        subprocess.run(
            [godot, "--headless", "--path", str(stage), "--editor", "--quit-after", "120", "--max-fps", "60"],
            env=env,
            check=True,
            timeout=180,
        )
        module.run_export(
            [godot, "--headless", "--path", str(stage), "--export-debug", preset, str(apk)],
            env=env,
            timeout=900,
        )
        try:
            with zipfile.ZipFile(apk) as archive:
                if "lib/arm64-v8a/libkas3_godot.so" not in archive.namelist():
                    raise ValueError("The selected Android preset did not export the ARM64 AS3 runtime.")
        except zipfile.BadZipFile as error:
            raise ValueError("The exported APK is not a valid zip archive.") from error
        badging = subprocess.check_output([aapt, "dump", "badging", apk], text=True, timeout=30)
        package, activity = launch_activity(badging)
        install(adb, apk)
        if activity:
            component = package + "/" + activity
        else:
            resolved = shell(
                "cmd", "package", "resolve-activity", "--brief", "-a", "android.intent.action.MAIN",
                "-c", "android.intent.category.LAUNCHER", package,
            ).splitlines()
            component = resolved[-1].strip() if resolved else ""
        component = validate_component(package, component)
        result = shell("am", "start", "-W", "-n", component, timeout=60)
        print(result, flush=True)
        if "Status: ok" not in result:
            raise ValueError("Android could not launch this game.")
        quoted_pkg = shlex.quote(package)
        try:
            while subprocess.run([*adb, "shell", "pidof", quoted_pkg], stdout=subprocess.DEVNULL, timeout=15).returncode == 0:
                time.sleep(1)
        finally:
            try:
                subprocess.run([*adb, "shell", "am", "force-stop", quoted_pkg], check=False, timeout=10, stderr=subprocess.DEVNULL)
            except (OSError, subprocess.SubprocessError):
                pass


if __name__ == "__main__":
    module.install_sigterm_handler()
    if len(sys.argv) != 7:
        sys.exit("Usage: android_run.py <project> <preset> <serial> <sdk> <godot> <runtime>")
    try:
        run(Path(sys.argv[1]), sys.argv[2], sys.argv[3], Path(sys.argv[4]), sys.argv[5], Path(sys.argv[6]))
    except (OSError, ValueError, subprocess.SubprocessError) as failure:
        sys.exit(str(failure))
