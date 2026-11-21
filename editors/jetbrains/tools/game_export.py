"""Stage a Godot game with its AS3 runtime and export the selected Godot presets."""
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import zipfile


def install_sigterm_handler():
    """Turn SIGTERM into SystemExit so context managers and finally blocks clean up."""
    def _stop(signum, _frame):
        raise SystemExit(128 + signum)
    return signal.signal(signal.SIGTERM, _stop)


def terminate_process(process):
    """Stop a subprocess (and its POSIX process group when session-isolated) without leaving orphans."""
    if process.poll() is not None:
        return
    if os.name == "posix":
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except (ProcessLookupError, PermissionError):
            pass
    else:
        process.terminate()
    try:
        process.wait(timeout=3)
        return
    except subprocess.TimeoutExpired:
        pass
    if os.name == "posix":
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except (ProcessLookupError, PermissionError):
            pass
    else:
        process.kill()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        pass


def update_preset_options(text, section, options, require_preset=False):
    """Replace or append keys in `[<section>.options]` without disturbing other presets."""
    if not re.fullmatch(r"preset\.\d+", section):
        raise ValueError("Choose a valid export preset.")
    lines = text.splitlines(keepends=True)
    if require_preset and not any(line.strip() == f"[{section}]" for line in lines):
        raise ValueError("The selected export preset is missing.")
    header = f"[{section}.options]"
    start = next((i + 1 for i, line in enumerate(lines) if line.strip() == header), None)
    if start is None:
        if lines and not lines[-1].endswith(("\n", "\r")):
            lines[-1] += "\n"
        lines.append(header + "\n")
        start = len(lines)
    end = next((i for i in range(start, len(lines)) if lines[i].strip().startswith("[")), len(lines))
    kept = [line for line in lines[start:end] if line.partition("=")[0].strip() not in options]
    if kept and not kept[-1].endswith(("\n", "\r")):
        kept[-1] += "\n"
    lines[start:end] = kept + ["\n"] + [f"{k}={v}\n" for k, v in options.items()]
    return "".join(lines)


def stage_game(project, stage, runtime, android=False, private_files=()):
    project = Path(project).resolve()
    excluded = {".godot", ".git", ".idea", ".run", "build", ".build"}
    private = {Path(key).resolve() for key in private_files}

    def ignore(path, names):
        folder = Path(path)
        skip = excluded.intersection(names) if folder == project else set()
        if private:
            skip.update(name for name in names if (folder / name).resolve() in private)
        return skip

    shutil.copytree(project, stage, ignore=ignore)
    (stage / ".godot").mkdir()
    (stage / ".godot/extension_list.cfg").write_text("res://kas3.gdextension\n")
    credentials = project / ".godot/export_credentials.cfg"
    if credentials.is_file():
        staged = stage / ".godot/export_credentials.cfg"
        shutil.copyfile(credentials, staged)
        staged.chmod(0o600)
    if not android:
        return
    descriptor = stage / "kas3.gdextension"
    text = descriptor.read_text()
    relative = Path("bin/android/arm64-v8a") / runtime.name
    expected_path = "res://" + relative.as_posix()
    if re.search(r'(?m)^\s*android(?:\.[\w-]+)*\s*=\s*"' + re.escape(expected_path) + r'"', text) and runtime.is_file():
        destination = stage / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(runtime, destination)
    if not re.search(r"(?m)^\s*android(?:\.|\s*=)", text):
        if not runtime.is_file():
            raise ValueError("The Android ARM64 AS3 runtime is missing from this plugin.")
        destination = stage / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(runtime, destination)
        text, count = re.subn(r"(?m)^\[libraries\]\s*$", '[libraries]\nandroid.arm64 = "res://' +
                              destination.relative_to(stage).as_posix() + '"\n', text, count=1)
        if not count:
            raise ValueError("The AS3 extension descriptor needs a libraries section.")
        descriptor.write_text(text)


def prepare_store(stage, job):
    """Apply store-only settings in the private copy so Run can still install a debug APK."""
    if not job.get("store"):
        return []
    section = job["section"]
    options = {"application/export_project_only": "true"} if job["platform"] == "iOS" else {
        "gradle_build/use_gradle_build": "true", "gradle_build/export_format": "1", "package/signed": "true",
    }
    path = stage / "export_presets.cfg"
    path.write_text(update_preset_options(path.read_text(), section, options))
    if job["platform"] != "Android":
        return []
    relative = (job.get("gradleDir") or "res://android").removeprefix("res://")
    folder = (stage / relative / "build").resolve()
    if not folder.is_relative_to(stage.resolve()):
        raise ValueError("The Android Gradle build directory must be inside the project.")
    return [] if folder.is_dir() else ["--install-android-build-template"]


def product(artifact, job):
    if job["platform"] == "iOS" and job.get("projectOnly", job.get("store", False)):
        project = artifact.with_suffix(".xcodeproj")
        return project if project.is_dir() else None
    if artifact.is_file() or (job["platform"] != "iOS" and artifact.is_dir()):
        return artifact
    if job["platform"] == "iOS" and "projectOnly" not in job and not job.get("store"):
        project = artifact.with_suffix(".xcodeproj")
        return project if project.is_dir() else None
    return None


def verify_play_bundle(path):
    try:
        with zipfile.ZipFile(path) as bundle:
            files = bundle.namelist()
    except zipfile.BadZipFile as error:
        raise ValueError("Godot did not produce a valid Android App Bundle.") from error
    if "base/manifest/AndroidManifest.xml" not in files:
        raise ValueError("Godot did not produce an Android App Bundle.")
    if not any(re.fullmatch(r"META-INF/[^/]+\.(RSA|DSA|EC)", f, re.I) for f in files):
        raise ValueError("The bundle is unsigned. Configure the Android upload key and rebuild.")
    if "base/lib/arm64-v8a/libkas3_godot.so" not in files:
        raise ValueError("The bundle is missing the ARM64 AS3 runtime.")


def validate_web_preset(stage, preset):
    # Read only the scalar fields used by Web. Other export options can span multiple lines.
    sections = {}
    fields = {}
    for line in (stage / "export_presets.cfg").read_text().splitlines():
        line = line.strip()
        if line.startswith("[") and "]" in line:
            fields = sections.setdefault(line[1:line.index("]")], {})
            continue
        key, _, value = line.partition("=")
        if key.strip() in ("name", "platform", "variant/extensions_support", "variant/thread_support"):
            try:
                fields[key.strip()] = json.JSONDecoder().raw_decode(value.lstrip())[0]
            except json.JSONDecodeError:
                pass
    for name, fields in sections.items():
        if fields.get("platform") == "Web" and fields.get("name") == preset:
            options = sections.get(name + ".options", {})
            if options.get("variant/extensions_support") is not True or options.get("variant/thread_support") is True:
                raise ValueError(f"Enable GDExtension support and disable threads in Web preset {preset}.")
            return
    raise ValueError(f"Missing Web preset {preset}.")


def stage_web_runtime(stage, native):
    descriptor = stage / "kas3.gdextension"
    text = descriptor.read_text()
    libraries = re.search(r"(?ms)^\[libraries\][ \t]*\n(.*?)(?=^\[|\Z)", text)
    if not libraries:
        raise ValueError("The AS3 extension descriptor needs a libraries section.")
    entries = re.findall(r'(?m)^[ \t]*([\w.-]+)[ \t]*=[ \t]*"([^"]+)"', libraries[1])
    paths = {value.removeprefix("res://") for tags, value in entries if "web" in tags.split(".")}
    additions = []
    standard = "bin/web/libkas3_godot.wasm"
    if not paths:
        paths.add(standard)
        additions.append(f'web.wasm32 = "res://{standard}"')
    available = set()
    for relative in paths:
        destination = (stage / relative).resolve()
        if not destination.is_relative_to(stage.resolve()):
            raise ValueError("The Web runtime must be inside the project.")
        bundled = native / "web/libkas3_godot.wasm"
        if relative == standard and bundled.is_file():
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(bundled, destination)
        if destination.is_file():
            available.add(destination.name)
    if not available:
        raise ValueError("Missing Web AS3 runtime at " + ", ".join(sorted(paths)) + ". Install the Kumikae Web kit.")
    # Stage the host editor extension alongside the Web runtime so Godot runs AS3IOSExport and packs
    # res://.kas3/program.kas3 for the slim runtime-only WebAssembly library.
    for platform, name in (("macos", "libkas3_godot.dylib"), ("linux", "libkas3_godot.so"), ("windows", "kas3_godot.dll")):
        host = native / name
        if host.is_file():
            (stage / "bin").mkdir(parents=True, exist_ok=True)
            shutil.copy2(host, stage / "bin" / name)
            if not any(platform in tags.split(".") for tags, _ in entries):
                additions.append(f'{platform} = "res://bin/{name}"')
    if additions:
        start = libraries.start(1)
        descriptor.write_text(text[:start] + "\n".join(additions) + "\n" + text[start:])
    return available


def stage_ios_runtime(stage, native):
    framework = stage / "bin/ios/kas3_godot.xcframework"
    source = native / "ios/kas3_godot.xcframework"
    exporter = native / "libkas3_godot.dylib"
    if source.is_dir() and exporter.is_file():
        if framework.exists():
            shutil.rmtree(framework)
        shutil.copytree(source, framework)
        shutil.copy2(exporter, stage / "bin" / exporter.name)
    elif not framework.is_dir():
        if not source.is_dir():
            raise ValueError("The iOS AS3 runtime is missing from this plugin.")
        shutil.copytree(source, framework)


def run_export(command, env=None, timeout=1800):
    missing_runtime = False
    native_error = ""
    password = (env or {}).get("GODOT_ANDROID_KEYSTORE_RELEASE_PASSWORD")
    timed_out = threading.Event()
    engine = subprocess.Popen(
        [str(value) for value in command],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
        start_new_session=(os.name == "posix"),
    )

    def on_timeout():
        timed_out.set()
        terminate_process(engine)

    timer = threading.Timer(timeout, on_timeout) if timeout else None
    if timer is not None:
        timer.daemon = True
        timer.start()
    try:
        for line in engine.stdout:
            if password:
                line = line.replace(password, "••••")
            print(line, end="", flush=True)
            if "ERROR: ActionScript:" in line:
                native_error = native_error or "AS3 compilation failed. See the compiler diagnostics above."
            if "ERROR: Native AS3: " in line:
                native_error = line.split("ERROR: Native AS3: ", 1)[1].strip()
            if "library found for GDExtension:" in line and "kas3.gdextension" in line:
                missing_runtime = True
        engine.wait(timeout=5)
    except subprocess.TimeoutExpired:
        terminate_process(engine)
    finally:
        if timer is not None:
            timer.cancel()
        if engine.poll() is None:
            terminate_process(engine)
        if engine.stdout is not None:
            engine.stdout.close()
    if timed_out.is_set():
        raise ValueError(f"Godot export timed out after {timeout} seconds.")
    if native_error:
        raise ValueError(native_error)
    if missing_runtime:
        raise ValueError("The export is missing the AS3 runtime.")
    if engine.returncode:
        raise ValueError(f"Godot exited with code {engine.returncode}.")


def export(project, destination, godot, native, presets, keys=None, private_files=()):
    with tempfile.TemporaryDirectory(prefix="kumikae-export-") as temporary:
        stage = Path(temporary) / "game"
        platforms = {item["platform"] for item in presets}
        private = list(private_files) + [key["path"] for key in (keys or {}).values()]
        stage_game(project, stage, native / "android/arm64-v8a/libkas3_godot.so", "Android" in platforms, private)
        if "Android" in platforms:
            for name in ("libkas3_godot.dylib", "libkas3_godot.so", "kas3_godot.dll"):
                host = native / name
                if host.is_file() and (stage / "bin" / name).is_file():
                    shutil.copy2(host, stage / "bin" / name)
        web_libraries = stage_web_runtime(stage, native) if "Web" in platforms else set()
        if "iOS" in platforms:
            stage_ios_runtime(stage, native)
        base = [godot, "--headless", "--path", str(stage)]
        if platforms != {"Web"}:
            subprocess.run(base + ["--editor", "--quit-after", "120", "--max-fps", "60"], check=True, timeout=180)
        output = Path(tempfile.mkdtemp(prefix="Game-exports-", dir=destination))
        print(f"Export directory: {output}", flush=True)
        failed = []
        products = []
        try:
            for index, preset in enumerate(presets, 1):
                label = preset["name"]
                filename = Path(preset["filename"]).name
                if not filename or filename in {".", ".."}:
                    raise ValueError(f"Invalid export filename for {label}.")
                subdir = output / (str(index) + "-" + (re.sub(r"[^a-zA-Z0-9._-]+", "-", label) or "target"))
                subdir.mkdir()
                artifact = subdir / filename
                print(f"Exporting {label} to {artifact}", flush=True)
                flags = prepare_store(stage, preset)
                env = os.environ.copy()
                key = (keys or {}).get(preset.get("section"))
                if key and preset["platform"] == "Android":
                    for field in ("path", "user", "password"):
                        env["GODOT_ANDROID_KEYSTORE_RELEASE_" + field.upper()] = key[field]
                command = base + ["--language", "en", *flags, "--export-release", label, str(artifact)]
                try:
                    if preset["platform"] == "Web":
                        validate_web_preset(stage, label)
                    run_export(command, env)
                except ValueError as error:
                    failed.append(f"{label} ({error})")
                    continue
                produced = product(artifact, preset)
                if preset["platform"] == "Web" and not any((subdir / name).is_file() for name in web_libraries):
                    failed.append(f"{label} (enable GDExtension support in the Web preset)")
                elif not produced:
                    failed.append(label)
                else:
                    if preset.get("store") and preset["platform"] == "Android":
                        verify_play_bundle(produced)
                    products.append(str(produced))
            if failed:
                raise ValueError("Godot could not export: " + ", ".join(failed) + ". See the engine output above.")
            return products
        except BaseException:
            shutil.rmtree(output, ignore_errors=True)
            raise


if __name__ == "__main__":
    install_sigterm_handler()
    if len(sys.argv) not in (6, 7):
        sys.exit(
            "Usage: game_export.py <project> <destination> <godot> <native> <presets-json> "
            "[products-json (signing keys JSON on stdin)]"
        )
    try:
        keys = json.load(sys.stdin) if len(sys.argv) == 7 else None
        products = export(Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3], Path(sys.argv[4]), json.loads(sys.argv[5]), keys)
        if len(sys.argv) == 7:
            Path(sys.argv[6]).write_text(json.dumps(products))
    except (OSError, ValueError, KeyError, zipfile.BadZipFile, subprocess.SubprocessError) as error:
        sys.exit(str(error))
