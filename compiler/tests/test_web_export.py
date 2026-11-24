"""Check which scripts the Web preset puts in the precompiled program, using a real Godot export."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile


def check_process(done):
    # godotengine/godot#122158: EditorHelp can run its deferred script-documentation
    # callback after EditorNode is destroyed, even in a project with no extension.
    singleton = 'ERROR: Parameter "singleton" is null.\n   at: is_cmdline_mode (editor/editor_node.cpp:6618)\n'
    thread = ("WARNING: A Thread object is being destroyed without its completion having been realized.\n"
              "Please call wait_to_finish() on it to ensure correct cleanup.\n"
              "   at: ~Thread (drivers/apple/thread_apple.cpp:150)\n")
    assert done.returncode == 0, (done.returncode, done.stdout, done.stderr)
    assert done.stderr in ("", singleton, singleton + thread), done.stderr


def check_export(godot):
    fixture = Path(__file__).resolve().parent / "godot_project"
    with tempfile.TemporaryDirectory() as folder:
        project = Path(folder) / "game"
        (project / ".godot").mkdir(parents=True)
        (project / ".godot/extension_list.cfg").write_text("res://kas3.gdextension\n")
        shutil.copy2(fixture / "kas3.gdextension", project)
        (project / "bin/web").mkdir(parents=True)
        for host in (fixture / "bin").glob("*"):
            if host.is_file():
                shutil.copy2(host, project / "bin")
        # --export-pack collects files without executing the target library.
        (project / "bin/web/libkas3_godot.wasm").write_bytes(b"\0asm\x01\0\0\0")
        (project / "main.tscn").write_text('[gd_scene format=3]\n[node name="Main" type="Node"]\n')
        (project / "project.godot").write_text('config_version=5\n[application]\nconfig/name="Web export test"\nrun/main_scene="res://main.tscn"\n')
        for name in ("Included", "Excluded"):
            (project / (name + ".as")).write_text(f"package {{ public class {name} {{ public function marker{name}():void {{}} }} }}")
        (project / "export_presets.cfg").write_text(
            '[preset.0]\nname="Web"\nplatform="Web"\nexport_filter="all_resources"\n'
            'include_filter="*.as"\nexclude_filter="Excluded.as"\n[preset.0.options]\n'
            'variant/extensions_support=true\nvariant/thread_support=false\n')
        dest = Path(folder) / "game.zip"
        done = subprocess.run([godot, "--headless", "--path", str(project), "--export-pack", "Web", str(dest)],
                              capture_output=True, text=True, timeout=120)
        check_process(done)
        with zipfile.ZipFile(dest) as archive:
            module = archive.read(".kas3/program.kas3")
            assert "Excluded.as" not in archive.namelist()
            assert b"markerIncluded" in module
            assert b"markerExcluded" not in module, "Web module includes a script excluded by the preset"
    print("web export: OK")


if __name__ == "__main__":
    check_export(sys.argv[1] if len(sys.argv) > 1 else "godot")
