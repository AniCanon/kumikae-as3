# A scene edited while it runs: run_godot.py runs this in a copy of the project, where it may rewrite
# Restart.as. An edit to code that runs once starts the scene again; any other edit keeps it.
extends SceneTree

var stage := 0
var mark := 0 # The watch's looks when the last save was made.
var frames := 0
var frame_mark := 0
var first_id := 0
var changes := 0
var keeper: Node2D

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func put(version: String) -> void:
	var file := FileAccess.open("res://Restart.as", FileAccess.WRITE)
	file.store_string(FileAccess.get_file_as_string("res://restart_%s.txt" % version))
	file.close()
	mark = looks()
	frame_mark = frames

# How many times the watch has looked at the scripts (the language's watch_looks()). A save is acted on by
# the second look after it: the first finds the change, the next finds it settled and compiles. The tests
# wait for looks, never for the clock, so a busy machine changes nothing.
func looks() -> int:
	for i in Engine.get_script_language_count():
		var language := Engine.get_script_language(i)
		if language.has_method("watch_looks"):
			return language.call("watch_looks")
	return 0

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
	frames += 1
	# The watch acts on a save by its second look; a scene started again is in a few frames later, and
	# ten more looks are many frames. Past these, what was waited for did not happen, and the line that
	# shows it says so. (Frames alone would not do: many pass between two looks on a machine with nothing
	# else to do.)
	var acted := looks() >= mark + 2
	var late := looks() >= mark + 12
	if stage == 0 and current_scene and said() == "one":
		first_id = current_scene.get_instance_id()
		show("first", said(), "one")
		put("body")
		stage = 1
	elif stage == 1 and acted:
		show("a method edited: the new code runs", said(), "one!")
		show("a method edited and a field added: the scene is the same", current_scene.get_instance_id() == first_id, true)
		put("ready")
		stage = 2
	elif stage == 2 and (str(said()).begins_with("three") or late):
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
		frame_mark = frames
		stage = 3
	elif stage == 3 and (said() == "five!five" or frames - frame_mark > 2000):
		show("a save while the scene starts again: the scene has that save's code throughout", said(), "five!five")
		show("a save while the scene starts again: the scene starts once more", changes, 2)
		return true
	return frames > 200000
