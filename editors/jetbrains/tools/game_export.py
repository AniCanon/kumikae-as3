"""Stage a Godot game with its AS3 runtime and export the selected Godot presets."""
import json
import os
import zipfile
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


def stage_game(project, stage, runtime, android=False, private_files=()):
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
    if not re.fullmatch(r"preset\.\d+", section):
        raise ValueError("Choose a valid export preset.")
    options = {"application/export_project_only": "true"} if job["platform"] == "iOS" else {
        "gradle_build/use_gradle_build": "true", "gradle_build/export_format": "1", "package/signed": "true",
    }
    path = stage / "export_presets.cfg"
    lines = path.read_text().splitlines(keepends=True)
    header = f"[{section}.options]"
    start = next((i + 1 for i, line in enumerate(lines) if line.strip() == header), None)
    if start is None:
        lines.append("\n" + header + "\n")
        start = len(lines)
    end = next((i for i in range(start, len(lines)) if lines[i].strip().startswith("[")), len(lines))
    kept = [line for line in lines[start:end] if line.partition("=")[0].strip() not in options]
    lines[start:end] = kept + ["\n"] + [f"{k}={v}\n" for k, v in options.items()]
    path.write_text("".join(lines))
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
    with zipfile.ZipFile(path) as bundle:
        files = bundle.namelist()
        if "base/manifest/AndroidManifest.xml" not in files:
            raise ValueError("Godot did not produce an Android App Bundle.")
        if not any(re.fullmatch(r"META-INF/[^/]+\.(RSA|DSA|EC)", f, re.I) for f in files):
            raise ValueError("The bundle is unsigned. Configure the Android upload key and rebuild.")
        if not any(f.startswith("base/lib/arm64-v8a/") and "kas3" in f for f in files):
            raise ValueError("The bundle is missing the ARM64 AS3 runtime.")


def export(project, destination, godot, native, presets, keys=None):
    with tempfile.TemporaryDirectory(prefix="kumikae-export-") as temporary:
        stage = Path(temporary) / "game"
        platforms = {item["platform"] for item in presets}
        private = [key["path"] for key in (keys or {}).values()]
        stage_game(project, stage, native / "android/arm64-v8a/libkas3_godot.so", "Android" in platforms, private)
        if "Android" in platforms:
            for name in ("libkas3_godot.dylib", "libkas3_godot.so", "kas3_godot.dll"):
                host = native / name
                if host.is_file() and (stage / "bin" / name).is_file():
                    shutil.copy2(host, stage / "bin" / name)
        if "iOS" in platforms:
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
        base = [godot, "--headless", "--path", str(stage)]
        subprocess.run(base + ["--editor", "--quit-after", "120", "--max-fps", "60"], check=True, timeout=180)
        output = Path(tempfile.mkdtemp(prefix="Game-exports-", dir=destination))
        print(f"Export directory: {output}", flush=True)
        failed = []
        products = []
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
            missing_runtime = False
            native_error = ""
            password = env.get("GODOT_ANDROID_KEYSTORE_RELEASE_PASSWORD")
            with subprocess.Popen(base + ["--language", "en", *flags, "--export-release", label, str(artifact)], env=env,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace") as engine:
                for line in engine.stdout:
                    if password:
                        line = line.replace(password, "••••")
                    print(line, end="", flush=True)
                    if "ERROR: ActionScript: the project's scripts have errors" in line:
                        native_error = "AS3 compilation failed. See the compiler diagnostics above."
                    if "ERROR: Native AS3: " in line:
                        native_error = line.split("ERROR: Native AS3: ", 1)[1].strip()
                    if "library found for GDExtension:" in line and "kas3.gdextension" in line:
                        missing_runtime = True
            produced = product(artifact, preset)
            if native_error:
                failed.append(f"{label} ({native_error})")
            elif missing_runtime or engine.returncode or not produced:
                failed.append(label)
            else:
                if preset.get("store") and preset["platform"] == "Android":
                    verify_play_bundle(produced)
                products.append(str(produced))
        if failed:
            raise ValueError("Godot could not export: " + ", ".join(failed) + ". See the engine output above.")
        return products


if __name__ == "__main__":
    if len(sys.argv) not in (6, 7):
        sys.exit("Usage: game_export.py <project> <destination> <godot> <native> <presets-json>")
    try:
        keys = json.load(sys.stdin) if len(sys.argv) == 7 else None
        products = export(Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3], Path(sys.argv[4]), json.loads(sys.argv[5]), keys)
        if len(sys.argv) == 7:
            Path(sys.argv[6]).write_text(json.dumps(products))
    except (OSError, ValueError, KeyError, zipfile.BadZipFile, subprocess.SubprocessError) as error:
        sys.exit(str(error))
