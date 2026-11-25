"""Check which scripts the Web preset puts in the precompiled program, using a real Godot export."""
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import zipfile


# Godot issue 122158 describes deferred editor documentation work at shutdown.
# A stock headless editor can print this after it has finished writing a valid pack.
SHUTDOWN = re.compile(
    r'ERROR: Parameter "singleton" is null\.\n   at: is_cmdline_mode \(editor/editor_node\.cpp:\d+\)\n'
    r'(?:WARNING: A Thread object is being destroyed without its completion having been realized\.\n'
    r'Please call wait_to_finish\(\) on it to ensure correct cleanup\.\n'
    r'   at: ~Thread \((?:core/os/thread|drivers/apple/thread_apple)\.cpp:\d+\)\n)?'
)


def check_process(done):
    if not done.stderr:
        assert done.returncode == 0, (done.returncode, done.stdout)
        return
    assert SHUTDOWN.fullmatch(done.stderr), done.stderr
    # Windows reports the process termination status directly. The caller also checks the archive.
    aborted = (done.returncode & 0xffffffff) in (3, 0xC0000005, 0x40000015) if sys.platform == "win32" else False
    assert done.returncode in (0, -signal.SIGABRT, -signal.SIGSEGV) or aborted, done.returncode


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
            assert archive.testzip() is None
            module = archive.read(".kas3/program.kas3")
            assert "Excluded.as" not in archive.namelist()
            assert b"markerIncluded" in module
            assert b"markerExcluded" not in module, "Web module includes a script excluded by the preset"
    print("web export: OK")


if __name__ == "__main__":
    check_export(sys.argv[1] if len(sys.argv) > 1 else "godot")
