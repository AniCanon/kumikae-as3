# A scene edited while it runs: run_godot.py runs this in a copy of the project, where it may rewrite
# Restart.as. An edit to code that runs once starts the scene again; any other edit keeps it.
extends SceneTree

var stage := 0
var since := 0
var first_id := 0
var changes := 0
var keeper: Node2D

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func put(version: String) -> void:
	var file := FileAccess.open("res://Restart.as", FileAccess.WRITE)
	file.store_string(FileAccess.get_file_as_string("res://restart_%s.txt" % version))
	file.close()
	since = Time.get_ticks_msec()

func said():
	return current_scene.call("said") if current_scene else null

# The scripts compiled again now, not when the watch next looks: what the test needs to hit a moment.
func reload_now() -> void:
	for i in Engine.get_script_language_count():
		var language := Engine.get_script_language(i)
		if language.has_method("reload_scripts"):
			language.call("reload_scripts")

func _initialize() -> void:
	change_scene_to_file("res://restart.tscn")
	scene_changed.connect(func(): changes += 1)
	# A node with the same script outside the scene, as an autoload is: a restart does not make it again.
	keeper = Node2D.new()
	keeper.name = "Keeper"
	keeper.set_script(load("res://Restart.as"))
	root.add_child(keeper)

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
		# The fifth version is saved as Godot takes the old scene out: the new one is made (its constructors
		# ran the fourth version's code) and not yet in the tree.
		current_scene.tree_exited.connect(func():
			put("five")
			reload_now(), CONNECT_ONE_SHOT)
		changes = 0
		put("four")
		reload_now()
		stage = 3
	elif stage == 3 and (said() == "five!five" or waited > 8000):
		show("a save while the scene starts again: the scene has that save's code throughout", said(), "five!five")
		show("a save while the scene starts again: the scene starts once more", changes, 2)
		return true
	return Time.get_ticks_msec() > 60000
