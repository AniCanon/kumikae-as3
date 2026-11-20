"""Verify and install the AS3 APK and resolve its launchable Android component."""
import re
import subprocess
import zipfile


def verify_runtime(apk):
    try:
        with zipfile.ZipFile(apk) as archive:
            if "lib/arm64-v8a/libkas3_godot.so" not in archive.namelist():
                raise ValueError("The exported APK is missing the ARM64 AS3 runtime.")
    except zipfile.BadZipFile as error:
        raise ValueError("The exported APK is not a valid zip archive.") from error


def install(sdk, adb, apk, android_run):
    versions = [p for p in (sdk / "build-tools").glob("*/aapt") if re.fullmatch(r"\d+(\.\d+)*", p.parent.name)]
    if not versions:
        raise ValueError("Install Android SDK Build Tools to inspect the exported APK.")
    aapt = max(versions, key=lambda p: tuple(map(int, p.parent.name.split("."))))
    verify_runtime(apk)
    badging = subprocess.check_output([aapt, "dump", "badging", apk], text=True, timeout=30)
    package, activity = android_run.launch_activity(badging)
    android_run.install(adb, apk)
    if activity:
        return package, package + "/" + activity
    resolved = subprocess.check_output([*adb, "shell", "cmd", "package", "resolve-activity", "--brief",
                                        "-a", "android.intent.action.MAIN", "-c",
                                        "android.intent.category.LAUNCHER", package], text=True, timeout=30).strip().splitlines()
    if not resolved or not resolved[-1].startswith(package + "/"):
        raise ValueError("Android could not resolve a launcher for " + package)
    return package, resolved[-1]


def launch(adb, component):
    result = subprocess.check_output([*adb, "shell", "am", "start", "-S", "-W", "-n", component], text=True, timeout=60)
    if "Status: ok" not in result:
        raise ValueError("Android could not launch the game. " + result.strip())
    return result
