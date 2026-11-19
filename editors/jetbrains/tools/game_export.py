"""Stage a Godot game with its AS3 runtime and export the selected Godot presets."""
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


def stage_game(project, stage, runtime, android=False):
    excluded = {".godot", ".git", ".idea", ".run", "build", ".build"}
    shutil.copytree(project, stage, ignore=lambda path, names: excluded.intersection(names) if Path(path) == project else [])
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



def export(project, destination, godot, native, presets):
    with tempfile.TemporaryDirectory(prefix="kumikae-export-") as temporary:
        stage = Path(temporary) / "game"
        platforms = {item["platform"] for item in presets}
        stage_game(project, stage, native / "android/arm64-v8a/libkas3_godot.so", "Android" in platforms)
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
        for index, preset in enumerate(presets, 1):
            label = preset["name"]
            filename = Path(preset["filename"]).name
            if not filename or filename in {".", ".."}:
                raise ValueError(f"Invalid export filename for {label}.")
            subdir = output / (str(index) + "-" + (re.sub(r"[^a-zA-Z0-9._-]+", "-", label) or "target"))
            subdir.mkdir()
            artifact = subdir / filename
            print(f"Exporting {label} to {artifact}", flush=True)
            missing_runtime = False
            native_error = ""
            with subprocess.Popen(base + ["--language", "en", "--export-release", label, str(artifact)],
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace") as engine:
                for line in engine.stdout:
                    print(line, end="", flush=True)
                    if "ERROR: Native AS3: " in line:
                        native_error = line.split("ERROR: Native AS3: ", 1)[1].strip()
                    if "library found for GDExtension:" in line and "kas3.gdextension" in line:
                        missing_runtime = True
            produced = artifact.is_file() or artifact.is_dir() or (
                preset["platform"] == "iOS" and artifact.with_suffix(".xcodeproj").is_dir()
            )
            if native_error:
                failed.append(f"{label} ({native_error})")
            elif missing_runtime or engine.returncode or not produced:
                failed.append(label)
        if failed:
            raise ValueError("Godot could not export: " + ", ".join(failed) + ". See the engine output above.")


if __name__ == "__main__":
    if len(sys.argv) != 6:
        sys.exit("Usage: game_export.py <project> <destination> <godot> <native> <presets-json>")
    try:
        export(Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3], Path(sys.argv[4]), json.loads(sys.argv[5]))
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        sys.exit(str(error))
