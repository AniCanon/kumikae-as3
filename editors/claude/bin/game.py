"""Build and launch a Godot export using the matching Kumikae runtime."""
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tempfile

import android_package
import android_run
import game_export
from mobile_runner import resolve_android_sdk, resolve_ios_device
from simulator_suite import setting

PLATFORMS = {"desktop": "macOS", "ios-simulator": "iOS", "android": "Android"}


def preset_for(project, target, requested):
    cfg = (project / "export_presets.cfg").read_text()
    names = [setting(cfg, group, "name") for group in re.findall(r"(?m)^\[(preset\.\d+)\]\s*$", cfg)
             if setting(cfg, group, "platform") == PLATFORMS[target]]
    if requested and requested in names:
        return requested
    if requested or len(names) != 1:
        raise ValueError(f"Choose one {PLATFORMS[target]} export preset with --preset. Available presets: {names}")
    return names[0]


def device_for(target, device):
    if target == "ios-simulator":
        return resolve_ios_device(device, require_booted=True)
    if target == "android":
        adb = resolve_android_sdk() / "platform-tools/adb"
        rows = subprocess.check_output([adb, "devices"], text=True, timeout=15).splitlines()
        devices = [row.split()[0] for row in rows if re.fullmatch(r"emulator-\d+\s+device", row)]
        if not devices:
            raise ValueError("Boot an ARM64 Android emulator before running the game.")
        if device in devices:
            return device
        if device or len(devices) != 1:
            raise ValueError("Choose a running emulator with --android-device from " + ", ".join(devices))
        return devices[0]
    return ""


def build(kit, project, target, preset, godot):
    required = {"desktop": (), "android": ("android/arm64-v8a/libkas3_godot.so",),
                "ios-simulator": ("ios/kas3_godot.xcframework", "ios-with-arm64-simulator.zip")}
    for name in required[target]:
        if not (kit / name).exists():
            raise ValueError(f"The {target} kit is missing {name}.")
    base = project / ".godot/kumikae-builds"
    base.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix=target + "-", dir=base))
    try:
        if target == "ios-simulator":
            proc = subprocess.run(["sh", kit / "tools/build-ios-simulator.sh", project, output, godot,
                                   kit / "ios-with-arm64-simulator.zip", kit / "ios", preset],
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=1200)
            if proc.returncode:
                raise ValueError(proc.stdout.strip() or "iOS Simulator build failed.")
            lines = proc.stdout.strip().splitlines()
            if not lines:
                raise ValueError("Simulator build did not report a product.")
            product = Path(lines[-1])
        else:
            with tempfile.TemporaryDirectory() as tmp:
                stage = Path(tmp) / "game"
                runtime = kit / "android/arm64-v8a/libkas3_godot.so"
                game_export.stage_game(project, stage, runtime, android=target == "android")
                env = dict(os.environ)
                if target == "android":
                    env["ANDROID_HOME"] = env["ANDROID_SDK_ROOT"] = str(resolve_android_sdk())
                base_command = [godot, "--headless", "--path", str(stage)]
                game_export.run_export(base_command + ["--editor", "--quit-after", "120", "--max-fps", "60"], env)
                product = output / ("Game.apk" if target == "android" else "Game.app")
                game_export.run_export(base_command + ["--export-debug", preset, str(product)], env)
        if not product.exists():
            raise ValueError("Export finished without creating the expected product.")
        if target == "android":
            android_package.verify_runtime(product)
        return product
    except BaseException:
        shutil.rmtree(output, ignore_errors=True)
        raise


def launch_mobile(project, target, device, product):
    if target == "ios-simulator":
        with (product / "Info.plist").open("rb") as stream:
            bundle = plistlib.load(stream).get("CFBundleIdentifier")
        if not bundle:
            raise ValueError("The built iOS app has no bundle identifier.")
        subprocess.run(["xcrun", "simctl", "install", device, product], check=True, timeout=120)
        subprocess.run(["xcrun", "simctl", "launch", "--terminate-running-process", device, bundle], check=True, timeout=60,
                       env=dict(os.environ, SIMCTL_CHILD_KAS3_SOURCE_DIR=str(project)))
        return
    sdk = resolve_android_sdk()
    adb = [str(sdk / "platform-tools/adb"), "-s", device]
    _, component = android_package.install(sdk, adb, product, android_run)
    print(android_package.launch(adb, component), end="")
