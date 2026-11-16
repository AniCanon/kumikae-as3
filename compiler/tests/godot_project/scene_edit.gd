# A scene edited in the folder the game was told of (KAS3_SOURCE_DIR), not in the project the game runs
# from: run_godot.py runs this in a copy of the project, with a copy of its files as that folder.
extends SceneTree

var stage := 0
var mark := 0 # The watch's looks when the scene was saved.
var frames := 0

# How many times the watch has looked at the scripts (the language's watch_looks()). A save is acted on by
# the second look after it: the first finds the change, the next finds it settled and compiles. The tests
# wait for looks, never for the clock, so a busy machine changes nothing.
func looks() -> int:
	for i in Engine.get_script_language_count():
		var language := Engine.get_script_language(i)
		if language.has_method("watch_looks"):
			return language.call("watch_looks")
	return 0

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func _initialize() -> void:
	change_scene_to_file("res://restart.tscn")

func _process(_delta: float) -> bool:
	frames += 1
	if stage == 0 and current_scene:
		show("the scene from the project", current_scene.position.x, 0.0)
		var place := OS.get_environment("KAS3_SOURCE_DIR").path_join("restart.tscn")
		var text := FileAccess.get_file_as_string(place) + "position = Vector2(7, 0)\n"
		var file := FileAccess.open(place, FileAccess.WRITE)
		file.store_string(text)
		file.close()
		mark = looks()
		stage = 1
	elif stage == 1 and ((current_scene and current_scene.position.x == 7) or looks() >= mark + 12):
		show("the scene edited in the folder: it starts again, from the edited file", current_scene.position.x if current_scene else null, 7.0)
		var later: Node2D = (load("res://restart.tscn") as PackedScene).instantiate()
		show("and what loads it later gets the edited one", later.position.x, 7.0)
		later.free()
		return true
	return frames > 200000
