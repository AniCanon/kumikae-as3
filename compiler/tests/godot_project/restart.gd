# A scene edited while it runs: run_godot.py runs this in a copy of the project, where it may rewrite
# Restart.as. An edit to code that runs once starts the scene again; any other edit keeps it.
extends SceneTree

var stage := 0
var since := 0
var first_id := 0

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func put(version: String) -> void:
	var file := FileAccess.open("res://Restart.as", FileAccess.WRITE)
	file.store_string(FileAccess.get_file_as_string("res://restart_%s.txt" % version))
	file.close()
	since = Time.get_ticks_msec()

func said():
	return current_scene.call("said") if current_scene else null

func _initialize() -> void:
	change_scene_to_file("res://restart.tscn")

func _process(_delta: float) -> bool:
	var waited := Time.get_ticks_msec() - since
	if stage == 0 and current_scene and said() == "one":
		first_id = current_scene.get_instance_id()
		show("first", said(), "one")
		put("body")
		stage = 1
	elif stage == 1 and (said() == "one!" or waited > 8000):
		show("a method edited: the new code runs", said(), "one!")
		show("a method edited and a field added: the scene is the same", current_scene.get_instance_id() == first_id, true)
		put("ready")
		stage = 2
	elif stage == 2 and (str(said()).begins_with("three") or waited > 8000):
		show("_ready edited: the scene starts again", said(), "three!")
		show("_ready edited: a new scene", current_scene.get_instance_id() != first_id, true)
		return true
	return Time.get_ticks_msec() > 60000
