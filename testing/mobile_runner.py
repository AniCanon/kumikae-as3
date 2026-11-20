"""Stage and stream discovered AS3 engine test suites on Web, iOS Simulator or Android Emulator."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time

_HERE = Path(__file__).resolve().parent


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_discovery = _load("kas3_mobile_discovery", _HERE / "discovery.py")
_suite = _load("kas3_mobile_suite", _HERE / "simulator_suite.py")


def stage_project(project_root, staged_root, case, filters):
    """Copy the project to staged_root and inject the generated TestRunner scene inside tests/."""
    # Exporters rewrite files in place. The stage must own every file it can modify.
    shutil.copytree(project_root, staged_root, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns(".git", ".idea", ".run", ".godot", ".build", "build"))
    runner_dir = staged_root / "tests" / "kas3_runner"
    if runner_dir.exists():
        raise ValueError("tests/kas3_runner is reserved for the generated test scene.")
    runner_dir.mkdir(parents=True)
    for tier in _discovery.missing_framework_tiers(staged_root):
        shutil.copytree(_HERE / tier / "app", runner_dir / "app", dirs_exist_ok=True)

    _discovery.godot_entry(runner_dir, case, filters=filters)
    # The scene uses res://tests/kas3_runner/Kas3EngineDriver.as directly; remove the desktop GDScript bootstrap.
    bootstrap = runner_dir / "run_tests.gd"
    if bootstrap.is_file():
        bootstrap.unlink()

    scene_path = "res://tests/kas3_runner/TestRunner.tscn"
    (runner_dir / "TestRunner.tscn").write_text(
        '[gd_scene load_steps=2 format=3]\n\n'
        '[ext_resource type="Script" path="res://tests/kas3_runner/Kas3EngineDriver.as" id="1"]\n\n'
        '[node name="TestRunner" type="Node"]\n'
        'script = ExtResource("1")\n'
    )
    game_path = staged_root / "project.godot"
    game_text = game_path.read_text()
    game_text = _suite.setting(game_text, "application", "run/main_scene", scene_path)
    game_path.write_text(game_text)
    return scene_path


def resolve_ios_device(device="", require_booted=False):
    chosen = (device or os.environ.get("KAS3_TEST_DEVICE", "")).strip()
    if chosen and not require_booted:
        return chosen
    result = subprocess.run(["xcrun", "simctl", "list", "devices", "booted", "-j"], capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise ValueError(result.stderr.strip() or "Failed to query booted iOS Simulators.")
    data = json.loads(result.stdout or "{}")
    booted = [
        item["udid"]
        for runtime, devices in data.get("devices", {}).items() if ".iOS-" in runtime
        for item in devices
        if item.get("state") == "Booted" and item.get("udid")
    ]
    if chosen:
        if chosen not in booted:
            raise ValueError("Select a booted iOS Simulator with --ios-device.")
        return chosen
    if len(booted) == 1:
        return booted[0]
    if not booted:
        raise ValueError("Boot an iOS Simulator or pass --device <udid> to run iOS tests.")
    raise ValueError("Multiple iOS Simulators are booted; pass --device <udid>.")


def resolve_android_sdk(sdk=""):
    home = Path.home()
    candidates = [
        sdk,
        os.environ.get("ANDROID_HOME", ""),
        os.environ.get("ANDROID_SDK_ROOT", ""),
        str(home / "Library/Android/sdk"),
        str(home / "Android/Sdk"),
    ]
    adb_name = "platform-tools/adb.exe" if os.name == "nt" else "platform-tools/adb"
    for item in candidates:
        if item and (Path(item) / adb_name).is_file():
            return Path(item)
    raise ValueError("Set ANDROID_HOME or pass --android-sdk to run Android tests.")


def resolve_android_device(adb, device=""):
    chosen = (device or os.environ.get("KAS3_TEST_DEVICE", "")).strip()
    if chosen:
        return chosen
    result = subprocess.run([str(adb), "devices"], capture_output=True, text=True, timeout=15)
    if result.returncode:
        raise ValueError(result.stderr.strip() or "Failed to list Android devices.")
    serials = []
    for line in result.stdout.splitlines()[1:]:
        parts = line.split()
        if len(parts) >= 2 and parts[1] == "device":
            serials.append(parts[0])
    if len(serials) == 1:
        return serials[0]
    if not serials:
        raise ValueError("Start an ARM64 Android emulator or pass --device <serial>.")
    raise ValueError("Multiple Android devices are connected; pass --device <serial>.")


def _kit_paths():
    native_root = _HERE.parent
    build = native_root / "tools/build-ios-simulator.sh"
    if not build.is_file():
        build = native_root / "editors/jetbrains/tools/build-ios-simulator.sh"
    export_mod = native_root / "tools/game_export.py"
    if not export_mod.is_file():
        export_mod = native_root / "editors/jetbrains/tools/game_export.py"
    return native_root, build, export_mod


def run_ios_simulator(project_root, case, filters, device, preset, godot):
    udid = resolve_ios_device(device)
    native_root, build_script, _ = _kit_paths()
    template = native_root / "ios-with-arm64-simulator.zip"
    ios_library = native_root / "ios"
    if not template.is_file():
        template = native_root / "spike/third_party/godot-ios-template/ios-with-arm64-simulator.zip"
    if not ios_library.is_dir():
        ios_library = native_root / "compiler/tests/godot_project/bin/ios"

    with tempfile.TemporaryDirectory(prefix="kas3-ios-tests-") as tmp:
        stage = Path(tmp) / "project"
        stage.mkdir()
        scene_path = stage_project(project_root, stage, case, filters)
        exports = (stage / "export_presets.cfg").read_text()
        game = (stage / "project.godot").read_text()
        game, cfg, group, bundle = _suite.prepare(game, exports, preset, scene_path)
        (stage / "project.godot").write_text(game)
        (stage / "export_presets.cfg").write_text(cfg)

        build = subprocess.run(
            [
                "/bin/sh",
                str(build_script),
                str(stage),
                str(Path(tmp) / "ios-build"),
                godot,
                str(template.resolve()),
                str(ios_library.resolve()),
                _suite.setting(cfg, group, "name"),
                group,
                bundle,
            ],
            capture_output=True,
            text=True,
        )
        if build.returncode:
            sys.stderr.write(build.stdout + build.stderr)
            return build.returncode
        lines = [line.strip() for line in build.stdout.splitlines() if line.strip()]
        if not lines:
            raise ValueError("The Simulator build did not print the built .app path.")
        app = Path(lines[-1])
        with (app / "Info.plist").open("rb") as stream:
            actual_bundle = plistlib.load(stream).get("CFBundleIdentifier")
        if not actual_bundle:
            raise ValueError("The built test application Info.plist has no CFBundleIdentifier.")

        subprocess.run(["xcrun", "simctl", "terminate", udid, actual_bundle], capture_output=True, text=True, timeout=30)
        install = subprocess.run(["xcrun", "simctl", "install", udid, str(app)], capture_output=True, text=True, timeout=90)
        if install.returncode:
            raise ValueError((install.stderr or install.stdout or "Failed to install iOS test app.").strip())

        simulator_log = _load("kas3_simulator_log", _HERE / "simulator_log.py")
        return simulator_log.run(udid, actual_bundle, case)


def run_web(project, case, filters, preset, godot):
    native, _, exporter = _kit_paths()
    web = _load("kas3_web_game", exporter.with_name("web_game.py"))
    browser = _load("kas3_web_runner", _HERE / "web_runner.py")
    chrome = browser.browser()
    if (native / "compiler").is_dir():
        native = native / "compiler/tests/godot_project/bin"
    with tempfile.TemporaryDirectory(prefix="kas3-web-tests-") as tmp:
        stage = Path(tmp) / "game"
        stage_project(project, stage, case, filters)
        (stage / ".godot").mkdir()
        (stage / ".godot/extension_list.cfg").write_text("res://kas3.gdextension\n")
        cfg = stage / "export_presets.cfg"
        exports = cfg.read_text()
        group, name = _suite.select_preset(_suite.platform_presets(exports, "Web"), "Web", preset)
        cfg.write_text(_suite.include_test_folder(exports, group, case["discover"]))
        output = Path(tmp) / "export"
        output.mkdir()
        product = web.export_stage(stage, native, godot, name, output)
        return browser.run(web, product, chrome, case["marker"])


def run_android(project_root, case, filters, device, preset, sdk_path, godot):
    sdk = resolve_android_sdk(sdk_path)
    adb = sdk / ("platform-tools/adb.exe" if os.name == "nt" else "platform-tools/adb")
    serial = resolve_android_device(adb, device)
    native_root, _, export_mod_path = _kit_paths()
    game_export = _load("kas3_game_export", export_mod_path)
    android_run = _load("kas3_android_run", export_mod_path.with_name("android_run.py"))

    abi = subprocess.check_output([str(adb), "-s", serial, "shell", "getprop", "ro.product.cpu.abilist"], text=True, timeout=15)
    if "arm64-v8a" not in abi.strip().split(","):
        raise ValueError("Select an ARM64 Android device or emulator.")

    android_runtime = native_root / "android/arm64-v8a/libkas3_godot.so"
    if not android_runtime.is_file():
        android_runtime = native_root / "compiler/build-android-arm64/out/libkas3_godot.so"

    with tempfile.TemporaryDirectory(prefix="kas3-android-tests-") as tmp:
        stage = Path(tmp) / "staged"
        stage.mkdir()
        stage_project(project_root, stage, case, filters)
        export_stage = Path(tmp) / "export"
        game_export.stage_game(stage, export_stage, android_runtime, android=True)

        exports = (export_stage / "export_presets.cfg").read_text()
        group, preset_name = _suite.select_preset(_suite.platform_presets(exports, "Android"), "Android", preset)

        # A test APK must be installable alongside the developer's game.
        options = group + ".options"
        package = _suite.setting(exports, options, "package/unique_name")
        if not re.fullmatch(r"[a-zA-Z][\w$]*(?:\.[a-zA-Z$][\w$]*)+", package, re.ASCII):
            raise ValueError("Set package/unique_name in the Android export preset before running tests.")
        cfg = _suite.setting(exports, options, "package/unique_name", package + ".tests")
        cfg = _suite.include_test_folder(cfg, group, case["discover"])
        (export_stage / "export_presets.cfg").write_text(cfg)

        apk = Path(tmp) / "tests.apk"
        env = dict(os.environ, ANDROID_HOME=str(sdk), ANDROID_SDK_ROOT=str(sdk))
        subprocess.run(
            [godot, "--headless", "--path", str(export_stage), "--editor", "--quit-after", "120", "--max-fps", "60"],
            env=env,
            check=True,
            timeout=180,
        )
        subprocess.run(
            [godot, "--headless", "--path", str(export_stage), "--export-debug", preset_name, str(apk)],
            env=env,
            check=True,
            timeout=900,
        )
        android_package = _load("kas3_android_package", _HERE / "android_package.py")
        connection = [str(adb), "-s", serial]
        package, component = android_package.install(sdk, connection, apk, android_run)

        shell = [str(adb), "-s", serial, "shell"]
        packages = subprocess.check_output([*shell, "pm", "list", "packages", "--user", "current", "-U", package], text=True, timeout=15)
        uid = re.search(r"(?m)^package:" + re.escape(package) + r"\s+uid:(\d+)$", packages)
        if not uid:
            raise ValueError("Could not identify the installed Android test package.")
        since = subprocess.check_output([*shell, "date", "+%s.%N"], text=True, timeout=15).strip()
        logcat = subprocess.Popen(
            [str(adb), "-s", serial, "logcat", "-T", since, "--uid=" + uid[1], "-v", "raw", "-s", "godot:I"],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        marker = case.get("marker", "PASS discovered suite")
        done = threading.Event()

        def watch_pid():
            time.sleep(1.5)
            while not done.is_set():
                if subprocess.run([str(adb), "-s", serial, "shell", "pidof", package], stdout=subprocess.DEVNULL, timeout=15).returncode != 0:
                    done.set()
                    logcat.terminate()
                    return
                time.sleep(0.5)

        watcher = threading.Thread(target=watch_pid, daemon=True)
        try:
            android_package.launch(connection, component)
            watcher.start()
            for line in logcat.stdout:
                sys.stdout.write(line)
                sys.stdout.flush()
                if line.rstrip("\r\n") == marker:
                    done.set()
                    break
            return 0
        finally:
            done.set()
            if logcat.poll() is None:
                logcat.terminate()
                try:
                    logcat.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    logcat.kill()
            subprocess.run([str(adb), "-s", serial, "shell", "am", "force-stop", package], check=False, timeout=10)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runtime", choices=("ios-simulator", "android", "web"))
    parser.add_argument("project", type=Path)
    parser.add_argument("case_json", type=Path)
    parser.add_argument("--godot", default="godot")
    parser.add_argument("--device", default="")
    parser.add_argument("--preset", default="")
    parser.add_argument("--android-sdk", default="")
    args = parser.parse_args()
    payload = json.loads(args.case_json.read_text())
    case = payload["case"]
    filters = payload.get("filters", [])
    if args.runtime == "web":
        return run_web(args.project.resolve(), case, filters, args.preset, args.godot)
    if args.runtime == "ios-simulator":
        return run_ios_simulator(args.project.resolve(), case, filters, args.device, args.preset, args.godot)
    return run_android(args.project.resolve(), case, filters, args.device, args.preset, args.android_sdk, args.godot)


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(130))
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
