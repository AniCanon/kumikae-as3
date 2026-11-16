# The game's own scripts load and run: run_godot.py runs this in copies of the project that have a script
# with a mistake in a test folder, which a game that is run does not read.
extends SceneTree

func _initialize() -> void:
	var counted := RefCounted.new()
	counted.set_script(load("res://Counted.as"))
	counted.set("n", 6)
	print("VALUE the game's scripts run=%s|6" % str(counted.get("n")))
	quit()
