"""Check Android execution modes in real Godot exports of a disposable copy of the test game."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("godot", help="Godot executable with matching Android export templates installed")
    args = parser.parse_args()
    source = Path(__file__).resolve().parent / "godot_project"
    android_so = source.parents[1] / "build-android-arm64/out/libkas3_godot.so"
    if not android_so.is_file():
        raise SystemExit(f"Missing Android extension library: {android_so}")
    with tempfile.TemporaryDirectory(prefix="kas3-android-modes-") as folder:
        project = Path(folder) / "game"
        shutil.copytree(source, project, ignore=shutil.ignore_patterns(".godot", ".idea"))
        (project / ".godot").mkdir()
        (project / ".godot/extension_list.cfg").write_text("res://kas3.gdextension\n")
        target_so = project / "bin/android/arm64-v8a/libkas3_godot.so"
        target_so.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(android_so, target_so)
        gdext = project / "kas3.gdextension"
        gdext.write_text(gdext.read_text() + '\nandroid.arm64 = "res://bin/android/arm64-v8a/libkas3_godot.so"\n')

        keystore = Path(folder) / "release.jks"
        subprocess.run(
            [
                "keytool", "-genkeypair", "-v", "-keystore", str(keystore),
                "-storepass", "testpass", "-keypass", "testpass",
                "-alias", "upload", "-keyalg", "RSA", "-keysize", "2048",
                "-validity", "365", "-dname", "CN=Test",
            ],
            check=True,
            capture_output=True,
        )

        preset = project / "export_presets.cfg"
        base_preset = (
            '[preset.0]\nname="Android"\nplatform="Android"\nrunnable=true\n'
            'export_filter="all_resources"\ninclude_filter="*.as"\nexclude_filter=""\n'
            '[preset.0.options]\narchitectures/arm64-v8a=true\n'
            'package/unique_name="org.kumikae.as3test"\n'
            f'keystore/release="{keystore}"\n'
            'keystore/release_user="upload"\n'
            'keystore/release_password="testpass"\n'
        )
        command = [args.godot, "--headless", "--path", str(project)]
        subprocess.run(command + ["--editor", "--quit-after", "120", "--max-fps", "60"], check=True)
        for value, build, native in [
            (None, "debug", False), (None, "release", True), ("1", "debug", True), ("0", "release", False),
        ]:
            path = Path(folder) / f"{value}-{build}"
            path.mkdir()
            preset.write_text(base_preset + (f"actionscript/native_code={value}\n" if value else ""))
            apk = path / "Game.apk"
            subprocess.run(command + [f"--export-{build}", "Android", str(apk)], check=True)
            with zipfile.ZipFile(apk) as archive:
                names = set(archive.namelist())
            has_program = "lib/arm64-v8a/libkas3_program.so" in names
            assert has_program == native, (value, build, has_program, sorted(n for n in names if "kas3" in n))
            print(f"{value or 'default'} {build}: {'native' if native else 'interpreter'}", flush=True)


if __name__ == "__main__":
    main()
