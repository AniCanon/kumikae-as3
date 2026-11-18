# A scene edited while it runs: run_godot.py runs this in a copy of the project, where it may rewrite
# Restart.as. No edit starts the scene again by itself: objects whose constructors depend only on their
# arguments and constants are updated in place, the rest is said, and a restart that is asked for shows it.
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

# The running scene started again from its file: what a person asks for to see what a reload could not show.
func restart_scene() -> void:
	for i in Engine.get_script_language_count():
		var language := Engine.get_script_language(i)
		if language.has_method("restart_scene"):
			language.call("restart_scene")

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
		# run_godot.py knows what this is: the edit changes the constant where the game started as native code.
		show("the first edit: the width of an object alive", current_scene.call("width"), "?")
		show("the first edit: a field the game changed keeps what it holds", current_scene.call("moved"), 7.0)
		# Made where an object of another class had been, whose constructor's arguments are remembered:
		# they are not this one's (Restart.as, churn()).
		show("the first edit: an object a constructor that runs once made, in the place of one let go", current_scene.call("stamped"), "3/4")
		show("the first edit: an object made where another had been is made again from its own arguments", current_scene.call("paired"), "?")
		show("the first edit: an object that holds text, made after others of its class, is made again from its own arguments", current_scene.call("wordedNow"), "?")
		put("ready")
		stage = 2
	elif stage == 2 and acted:
		show("_ready edited: the scene does not start again by itself", current_scene.get_instance_id() == first_id, true)
		show("_ready edited: what it did is kept", said(), "one!")
		show("a constant edited: it has its new value", current_scene.call("limit"), "b")
		show("a constant edited: an object alive that was made from it is updated in place", current_scene.call("width"), 30.0)
		show("a constant edited: a field the game changed since keeps what it holds", current_scene.call("moved"), 7.0)
		show("a static variable's first value edited: it keeps the one it was given", current_scene.call("firstNow"), "a")
		restart_scene()
		mark = looks()
		stage = 21
	elif stage == 21 and (str(said()).begins_with("three") or late):
		show("a restart asked for: the scene starts again", said(), "three!")
		show("a restart asked for: a new scene", current_scene.get_instance_id() != first_id, true)
		# The fifth version is saved as Godot takes the old scene out: the new one is made (its constructors
		# ran the fourth version's code) and not yet in the tree.
		current_scene.tree_exited.connect(func():
			put("five")
			reload_now(), CONNECT_ONE_SHOT)
		changes = 0
		put("four")
		reload_now()
		restart_scene()
		frame_mark = frames
		stage = 3
	elif stage == 3 and (said() == "five!five" or frames - frame_mark > 2000):
		show("a save while a restart asked for is being made: the scene has that save's code throughout", said(), "five!five")
		show("a save while a restart asked for is being made: the scene starts once more", changes, 2)
		return true
	return frames > 200000
