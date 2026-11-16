# A scene edited in the folder the game was told of (KAS3_SOURCE_DIR), not in the project the game runs
# from: run_godot.py runs this in a copy of the project, with a copy of its files as that folder.
extends SceneTree

var stage := 0
var since := 0

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func _initialize() -> void:
	change_scene_to_file("res://restart.tscn")

func _process(_delta: float) -> bool:
	var waited := Time.get_ticks_msec() - since
	if stage == 0 and current_scene:
		show("the scene from the project", current_scene.position.x, 0.0)
		var place := OS.get_environment("KAS3_SOURCE_DIR").path_join("restart.tscn")
		var text := FileAccess.get_file_as_string(place) + "position = Vector2(7, 0)\n"
		var file := FileAccess.open(place, FileAccess.WRITE)
		file.store_string(text)
		file.close()
		since = Time.get_ticks_msec()
		stage = 1
	elif stage == 1 and ((current_scene and current_scene.position.x == 7) or waited > 8000):
		show("the scene edited in the folder: it starts again, from the edited file", current_scene.position.x if current_scene else null, 7.0)
		var later: Node2D = (load("res://restart.tscn") as PackedScene).instantiate()
		show("and what loads it later gets the edited one", later.position.x, 7.0)
		later.free()
		return true
	return Time.get_ticks_msec() > 60000
