"""The new-project template runs in Godot: a swipe sends the disc that way, a tap stops it, and a second
finger or a release with no press seen does nothing. Skipped, with the reason, without Godot 4.7 on the
PATH or the compiler's own build beside this plugin (compiler/build-godot)."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
TEMPLATE = ROOT / "editors/jetbrains/src/main/kotlin/app/anicanon/kumikae/as3/project/ProjectTemplate.kt"
LIBRARY = Path(os.environ.get("KAS3_ROOT", ROOT)) / "compiler/tests/godot_project/bin/libkas3_godot.dylib"

DRIVER = """extends SceneTree
var frames := 0
var disc: Node2D
var mark: Vector2
func touch(at: Vector2, down: bool, finger := 0) -> void:
	var event := InputEventScreenTouch.new()
	event.position = at
	event.pressed = down
	event.index = finger
	Input.parse_input_event(event)
func moved() -> Vector2:
	var by := disc.position - mark
	mark = disc.position
	return by
func _initialize() -> void:
	change_scene_to_file("res://scenes/main.tscn")
func _process(_delta: float) -> bool:
	frames += 1
	if frames == 3:
		disc = current_scene.get_node("Disc")
		mark = disc.position
		touch(Vector2(500, 10), false)
	elif frames == 8:
		print("RESULT release alone: ", moved().length() < 0.001)
		touch(Vector2(100, 100), true)
		touch(Vector2(400, 400), true, 1)
		touch(Vector2(400, 400), false, 1)
		touch(Vector2(300, 100), false)
	elif frames == 8 + 1:
		moved()
	elif frames == 20:
		var by := moved()
		print("RESULT swipe right: ", by.x > 0.5 and absf(by.y) < 0.001)
		touch(Vector2(50, 50), true)
		touch(Vector2(52, 51), false)
	elif frames == 22:
		moved()
	elif frames == 34:
		print("RESULT tap stops: ", moved().length() < 0.001)
		return true
	return false
"""


def constant(text, name):
    found = re.search(r'private const val %s = """(.*?)"""' % name, text, re.S)
    return found.group(1)


@unittest.skipUnless(shutil.which("godot"), "Godot is not on the PATH")
@unittest.skipUnless(LIBRARY.exists(), "the AS3 extension is not built (compiler/build-godot)")
class TemplateRunsInGodot(unittest.TestCase):
    def test_swipe_tap_and_stray_touches(self):
        text = TEMPLATE.read_text()
        with tempfile.TemporaryDirectory() as folder:
            game = Path(folder)
            source = "src/org/example/game/view"
            (game / source).mkdir(parents=True)
            (game / "scenes").mkdir()
            for name, file in (("SCRIPT", source + "/Main.as"), ("HELLO", source + "/Hello.as"),
                               ("SCENE", "scenes/main.tscn"), ("EXTENSION", "kas3.gdextension")):
                content = constant(text, name).replace("{source}", source)
                if file.endswith(".as"):
                    content = content.replace("package {", "package org.example.game.view {")
                (game / file).write_text(content)
            (game / "project.godot").write_text(constant(text, "PROJECT").replace("{name}", "Template"))
            (game / ".godot").mkdir()
            (game / ".godot/extension_list.cfg").write_text("res://kas3.gdextension\n")
            (game / "bin").mkdir()
            shutil.copy(LIBRARY, game / "bin")
            (game / "driver.gd").write_text(DRIVER)
            run = subprocess.run(["godot", "--headless", "--path", str(game), "-s", "driver.gd"], capture_output=True,
                                 text=True, timeout=120)
            output = run.stdout + run.stderr
            self.assertNotIn("ERROR", output, output[-1500:])
            for result in ("release alone", "swipe right", "tap stops"):
                self.assertIn("RESULT %s: true" % result, output, output[-1500:])


if __name__ == "__main__":
    unittest.main()
