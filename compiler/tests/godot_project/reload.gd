# Scripts edited while the game runs: run_godot.py runs this in a copy of the project, where it may
# rewrite Hot.as. Each line shows what the AS3 script gave and what is expected.
extends SceneTree

var node: Node2D
var other: Node
var frames := 0
var stage := 0
var since := 0

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func ask(method: String, arguments: Array = []):
	return Callable(node, method).callv(arguments)

func hot_path() -> String:
	# Where a person edits the scripts: the project, or the folder the game was told they are in.
	var folder := OS.get_environment("KAS3_SOURCE_DIR")
	return ProjectSettings.globalize_path("res://Hot.as") if folder.is_empty() else folder + "/Hot.as"

func put(version: String) -> void:
	var text := FileAccess.get_file_as_string("res://hot_%s.txt" % version)
	var file := FileAccess.open(hot_path(), FileAccess.WRITE)
	file.store_string(text)
	file.close()
	since = Time.get_ticks_msec()

func waited(milliseconds: int) -> bool:
	return Time.get_ticks_msec() - since > milliseconds

func _initialize() -> void:
	node = Node2D.new()
	node.set_script(load("res://Hot.as"))
	root.add_child(node)
	other = Node.new()
	root.add_child(other)

func _process(_delta: float) -> bool:
	frames += 1
	if stage == 0 and frames == 2:
		ask("listen", [other])
		ask("bump")
		ask("bump")
		other.name = "first"
		show("before: value", ask("value"), 1)
		show("before: heard", ask("heard"), 1)
		put("v2")
		stage = 1
	elif stage == 1 and (ask("value") == 2 or waited(8000)):
		show("after an edit: the new code runs", ask("value"), 2)
		show("after an edit: a field keeps its value", ask("count"), 2)
		show("after an edit: a static field keeps its value", ask("total"), 2)
		show("after an edit: a vector field keeps its elements", ask("seenText"), "1,2")
		show("after an edit: a method that is new can be called", ask("added"), "new")
		other.name = "second"
		show("after an edit: a listener added before runs the new code", ask("heard"), 11)
		ask("unlisten", [other])
		other.name = "third"
		show("after an edit: a listener added before can be removed", ask("heard"), 11)
		put("broken")
		stage = 2
	elif stage == 2 and waited(1500):
		show("with an error: the game keeps the scripts it had", ask("value"), 2)
		put("v3")
		stage = 3
	elif stage == 3 and (ask("value") == 3 or waited(8000)):
		show("once the error is fixed: the new code runs", ask("value"), 3)
		show("once the error is fixed: the field still has its value", ask("count"), 2)
		# A save of the same size with the same time as the last (two saves within one second): still seen.
		var stamp := ProjectSettings.globalize_path("user://stamp")
		OS.execute("touch", ["-r", hot_path(), stamp])
		put("v4")
		OS.execute("touch", ["-r", stamp, hot_path()])
		stage = 4
	elif stage == 4 and (ask("value") == 4 or waited(8000)):
		show("a save of the same size in the same second is seen", ask("value"), 4)
		node.free()
		other.free()
		return true
	return false
