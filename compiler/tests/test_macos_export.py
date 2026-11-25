"""Export a small AS3 project as a macOS app and run it headless, for run_godot.py.

A release export with the default (Automatic) mode builds the scripts into `libkas3_program.dylib`, for
both of the universal preset's architectures, in the app's `Contents/Frameworks` beside the extension.
The app then says its scripts run as native code, and prints what the interpreter prints. A library built
from other scripts than the app's is refused with a warning, and the interpreter runs them. Without
developer tools for xcrun, an Automatic release export warns and leaves the scripts to the interpreter,
and a Native export fails.
"""
import glob
import os
import shutil
import subprocess
import sys
import tempfile

USED = "ActionScript: the scripts run as native code."
STALE = "it was built from other sources than the project's scripts are now"
NO_TOOLS = "the scripts will run on the interpreter"
LIBRARY = "libkas3_program.dylib"
EXTENSION = "libkas3_godot.dylib"
TEMPLATES = os.path.expanduser("~/Library/Application Support/Godot/export_templates")

MAIN = """package {
	import godot.*;

	public class Main extends Node {
		override public function _ready():void {
			var total:int = 0;
			for (var i:int = 0; i < 1000; i++) {
				total += i * STEP % 7;
			}
			var words:Array = ["lantern", "reach", "marsh"];
			words.sort();
			trace("RESULT total=" + total + " words=" + words.join(",") + " half=" + (total / 2));
			getTree().quit();
		}
	}
}
"""
SCENE = """[gd_scene load_steps=2 format=3]

[ext_resource type="Script" path="res://Main.as" id="1"]

[node name="Main" type="Node"]
script = ExtResource("1")
"""
PRESET = """[preset.0]
name="Desktop"
platform="macOS"
export_filter="all_resources"
include_filter="*.as"
exclude_filter=""
[preset.0.options]
application/bundle_identifier="org.kumikae.as3exporttest"
application/export_angle=0
"""


def lines(text, prefix="RESULT "):
    return [line for line in text.splitlines() if line.startswith(prefix)]


def app_binary(app):
    found = glob.glob(os.path.join(app, "Contents", "MacOS", "*"))
    return found[0] if len(found) == 1 else None


def run_app(app, env):
    binary = app_binary(app)
    if not binary:
        return None
    return subprocess.run([binary, "--headless", "--quit-after", "300"], capture_output=True, text=True, env=env, timeout=300)


def signed(path):
    done = subprocess.run(["codesign", "--verify", "--strict", path], capture_output=True, text=True, timeout=60)
    return done.returncode == 0


def archs(path):
    done = subprocess.run(["lipo", "-archs", path], capture_output=True, text=True, timeout=60)
    return sorted(done.stdout.split()) if done.returncode == 0 else []


def write_main(project, step):
    with open(os.path.join(project, "Main.as"), "w") as file:
        file.write(MAIN.replace("STEP", str(step)))


def export(godot, project, preset_text, app, env):
    with open(os.path.join(project, "export_presets.cfg"), "w") as file:
        file.write(preset_text)
    return subprocess.run([godot, "--headless", "--path", project, "--export-release", "Desktop", app],
                          capture_output=True, text=True, env=env, timeout=900)


def interpreted(godot, project, env):
    done = subprocess.run([godot, "--headless", "--path", project, "--quit-after", "300"], capture_output=True, text=True, env=env, timeout=300)
    return lines(done.stdout)


def check_export(godot, extension, check):
    """`extension` is the folder that holds kas3.gdextension and bin/libkas3_godot.dylib."""
    if sys.platform != "darwin":
        print("macos export: skipped (not macOS)")
        return
    version = subprocess.run([godot, "--version"], capture_output=True, text=True, timeout=60).stdout.strip()
    parts = version.split(".")
    status = next((i for i, part in enumerate(parts) if not part.isdigit()), len(parts) - 1)
    templates = os.path.join(TEMPLATES, ".".join(parts[:status + 1]), "macos.zip")
    if not os.path.isfile(templates):
        print("macos export: skipped (no macOS export templates for Godot %s)" % version)
        return
    env = dict(os.environ, KAS3_NATIVE_PLAY="0")
    env.pop("KAS3_PROGRAM_LIBRARY", None)
    with tempfile.TemporaryDirectory(prefix="kas3-macos-") as folder:
        project = os.path.join(folder, "game")
        os.makedirs(os.path.join(project, "bin"))
        shutil.copy2(os.path.join(extension, "kas3.gdextension"), project)
        shutil.copy2(os.path.join(extension, "bin", EXTENSION), os.path.join(project, "bin"))
        with open(os.path.join(project, "project.godot"), "w") as file:
            file.write('config_version=5\n[application]\nconfig/name="Export Test"\nrun/main_scene="res://main.tscn"\n'
                       '[rendering]\ntextures/vram_compression/import_etc2_astc=true\n')
        with open(os.path.join(project, "main.tscn"), "w") as file:
            file.write(SCENE)
        write_main(project, 3)
        os.makedirs(os.path.join(project, ".godot"))
        with open(os.path.join(project, ".godot", "extension_list.cfg"), "w") as file:
            file.write("res://kas3.gdextension\n")
        subprocess.run([godot, "--headless", "--editor", "--path", project, "--quit-after", "300"],
                       capture_output=True, text=True, env=env, timeout=300)
        want = interpreted(godot, project, env)
        check("macos export: the interpreter runs the project", len(want) == 1, repr(want))

        native = os.path.join(folder, "native", "Export Test.app")
        os.makedirs(os.path.dirname(native))
        done = export(godot, project, PRESET, native, env)
        library = os.path.join(native, "Contents", "Frameworks", LIBRARY)
        check("macos export: a release export puts the program library beside the extension",
              os.path.isfile(library) and os.path.isfile(os.path.join(native, "Contents", "Frameworks", EXTENSION)),
              (done.stdout + done.stderr)[-1500:])
        if not os.path.isfile(library):
            return
        check("macos export: the library is universal", archs(library) == ["arm64", "x86_64"], repr(archs(library)))
        check("macos export: the library is signed as the extension is", signed(library) and signed(os.path.join(native, "Contents", "Frameworks", EXTENSION)))
        ran = run_app(native, env)
        output = ran.stdout + ran.stderr if ran else "no binary in the app"
        check("macos export: the exported app runs its scripts as native code", ran is not None and USED in ran.stdout, output[-1500:])
        check("macos export: native code prints what the interpreter prints", ran is not None and lines(ran.stdout) == want, output[-1500:])
        check("macos export: the native app reports no error", ran is not None and "ERROR" not in output and "WARNING" not in output, output[-1500:])

        # Other scripts in an interpreter export, with the first export's library put beside the extension.
        write_main(project, 5)
        want = interpreted(godot, project, env)
        stale = os.path.join(folder, "stale", "Export Test.app")
        os.makedirs(os.path.dirname(stale))
        done = export(godot, project, PRESET + "actionscript/native_code=0\n", stale, env)
        placed = os.path.join(stale, "Contents", "Frameworks", LIBRARY)
        check("macos export: an interpreter export has no program library",
              os.path.isdir(os.path.join(stale, "Contents")) and not os.path.exists(placed), (done.stdout + done.stderr)[-1500:])
        shutil.copy2(library, placed)
        subprocess.run(["codesign", "--force", "--sign", "-", placed], capture_output=True, text=True, timeout=60)
        ran = run_app(stale, env)
        output = ran.stdout + ran.stderr if ran else "no binary in the app"
        check("macos export: a stale library is refused with the reason", ran is not None and STALE in output and USED not in output, output[-1500:])
        check("macos export: the interpreter then runs the scripts", ran is not None and lines(ran.stdout) == want and len(want) == 1, output[-1500:])

        # No developer tools for xcrun: Automatic release falls back to the interpreter, Native is an error.
        no_tools = dict(env, DEVELOPER_DIR=os.path.join(folder, "no-developer-tools"))
        plain = os.path.join(folder, "no-tools", "Export Test.app")
        os.makedirs(os.path.dirname(plain))
        done = export(godot, project, PRESET, plain, no_tools)
        said = done.stdout + done.stderr
        check("macos export: without the tools, an Automatic release export succeeds", done.returncode == 0 and "ERROR" not in said, said[-1500:])
        check("macos export: and warns that the interpreter will run the scripts", "WARNING" in said and NO_TOOLS in said, said[-1500:])
        check("macos export: and builds no program library",
              os.path.isdir(os.path.join(plain, "Contents")) and not os.path.exists(os.path.join(plain, "Contents", "Frameworks", LIBRARY)), said[-1500:])
        ran = run_app(plain, env)
        output = ran.stdout + ran.stderr if ran else "no binary in the app"
        check("macos export: that app runs on the interpreter",
              ran is not None and USED not in output and "WARNING" not in output and lines(ran.stdout) == want, output[-1500:])
        forced = os.path.join(folder, "no-tools-native", "Export Test.app")
        os.makedirs(os.path.dirname(forced))
        done = export(godot, project, PRESET + "actionscript/native_code=1\n", forced, no_tools)
        said = done.stdout + done.stderr
        check("macos export: without the tools, a Native export is an error", "ERROR" in said and NO_TOOLS not in said, said[-1500:])
    print("macos export: done")


if __name__ == "__main__":
    failures = []
    check_export(sys.argv[1] if len(sys.argv) > 1 else "godot", os.path.join(os.path.dirname(os.path.abspath(__file__)), "godot_project"),
                 lambda name, ok, detail="": ok or failures.append("%s %s" % (name, detail)))
    print("\n".join(failures) or "macos export: OK")
    sys.exit(1 if failures else 0)
