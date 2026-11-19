"""Check iOS execution modes in real Godot exports of a disposable copy of the test game."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("godot", help="Godot executable with matching iOS export templates installed")
    args = parser.parse_args()
    source = Path(__file__).resolve().parent / "godot_project"
    with tempfile.TemporaryDirectory(prefix="kas3-ios-modes-") as folder:
        project = Path(folder) / "game"
        shutil.copytree(source, project, ignore=shutil.ignore_patterns(".godot", ".idea"))
        (project / ".godot").mkdir()
        (project / ".godot/extension_list.cfg").write_text("res://kas3.gdextension\n")
        command = [args.godot, "--headless", "--path", str(project)]
        subprocess.run(command + ["--editor", "--quit-after", "120", "--max-fps", "60"], check=True)
        preset = project / "export_presets.cfg"
        original = preset.read_text()
        for value, build, native in [
            (None, "debug", False), (None, "release", True), ("2", "debug", False), ("2", "release", True),
            ("true", "debug", True), ("false", "release", False), ("1", "debug", True), ("0", "release", False),
        ]:
            path = Path(folder) / f"{value}-{build}"
            path.mkdir()
            preset.write_text(original + (f"\nactionscript/native_code={value}\n" if value else ""))
            subprocess.run(command + [f"--export-{build}", "iOS", str(path / "Game.ipa")], check=True)
            xcode = (path / "Game.xcodeproj/project.pbxproj").read_text()
            for sdk in ["iphoneos", "iphonesimulator"]:
                obj = path / f"Game/.kas3/{sdk}.o"
                assert obj.is_file() == native, (value, build, obj)
            assert ("$(PLATFORM_NAME).o" in xcode) == native, (value, build)
            print(f"{value or 'default'} {build}: {'native' if native else 'interpreter'}", flush=True)


if __name__ == "__main__":
    main()
