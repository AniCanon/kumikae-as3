# Scripts edited while the game runs: run_godot.py runs this in a copy of the project, where it may
# rewrite Hot.as. Each line shows what the AS3 script gave and what is expected.
extends SceneTree

var node: Node2D
var other: Node
var counted: RefCounted
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

func same_time() -> String:
	# A file whose time the saves of one step are all given.
	return ProjectSettings.globalize_path("user://stamp")

func put_at_same_time(version: String) -> void:
	put(version)
	OS.execute("touch", ["-r", same_time(), hot_path()])

func waited(milliseconds: int) -> bool:
	return Time.get_ticks_msec() - since > milliseconds

func _initialize() -> void:
	node = Node2D.new()
	node.set_script(load("res://Hot.as"))
	root.add_child(node)
	other = Node.new()
	root.add_child(other)
	counted = RefCounted.new()
	counted.set_script(load("res://Counted.as"))
	counted.set("n", 5)

func _process(_delta: float) -> bool:
	frames += 1
	if stage == 0 and frames == 2:
		ask("listen", [other])
		ask("listenJoy")
		ask("fill")
		ask("bump")
		ask("bump")
		ask("hold")
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
		# A class as a value, a method of the library as a value, and a property hidden from loops.
		show("after an edit: a class value, a method value and a hidden property are kept", ask("held"), "true true 3 b 1")
		show("after an edit: a vector of a type the new code does not name keeps its elements", ask("bagText"), "2")
		show("after an edit: a counted object keeps its field", counted.get("n"), 5)
		var weak: WeakRef = weakref(counted)
		counted = null
		show("after an edit: a counted object goes when let go", weak.get_ref() == null, true)
		other.name = "second"
		show("after an edit: a listener added before runs the new code", ask("heard"), 11)
		ask("unlisten", [other])
		other.name = "third"
		show("after an edit: a listener added before can be removed", ask("heard"), 11)
		# A singleton's listener, added before: it is the object's own still, for removeAll().
		Input.joy_connection_changed.emit(0, true)
		show("after an edit: a singleton's listener added before runs the new code", ask("heard"), 21)
		ask("clearJoy")
		Input.joy_connection_changed.emit(0, true)
		show("after an edit: removeAll() on a singleton's signal takes the object's own", ask("heard"), 21)
		put("broken")
		stage = 2
	elif stage == 2 and waited(1500):
		show("with an error: the game keeps the scripts it had", ask("value"), 2)
		# The error is a class that is not there: it is fixed by a file of its own, in a new folder.
		var folder := hot_path().get_base_dir().path_join("later")
		DirAccess.make_dir_recursive_absolute(folder)
		var later := FileAccess.open(folder.path_join("Later.as"), FileAccess.WRITE)
		later.store_string("package { public class Later { public static function zero():int { return 0; } } }\n")
		later.close()
		since = Time.get_ticks_msec()
		stage = 3
	elif stage == 3 and (ask("value") == 3 or waited(8000)):
		show("once the error is fixed, by a file in a new folder: the new code runs", ask("value"), 3)
		show("once the error is fixed: the field still has its value", ask("count"), 2)
		# Saves of the same size that carry the same time (as two within one second do) are told apart by
		# what they hold. The time is one set here, far ahead, and given to every save of this step: however
		# long this machine takes between a save and the look at it, the saves stay "within one second" of
		# each other and of that look. Nothing is waited for but the code each save brings.
		OS.execute("touch", ["-t", "203701010000", same_time()])
		put_at_same_time("v4")
		stage = 31
	elif stage == 31 and (ask("value") == 4 or waited(8000)):
		put_at_same_time("broken") # The code that gives 3, of the same size as the one that gives 4.
		stage = 32
	elif stage == 32 and (ask("value") == 3 or waited(8000)):
		show("a save of the same size at the same time is seen", ask("value"), 3)
		put_at_same_time("v4")
		stage = 4
	elif stage == 4 and (ask("value") == 4 or waited(8000)):
		show("a save of the same size in the same second is seen", ask("value"), 4)
		# A reload asked for while AS3 runs (AS3 emits a signal, GDScript hears it and asks): refused.
		var emitter := Node.new()
		emitter.set_script(load("res://Signals.as"))
		root.add_child(emitter)
		emitter.renamed.connect(func():
			for i in Engine.get_script_language_count():
				if Engine.get_script_language(i).has_method("reload_scripts"):
					Engine.get_script_language(i).call("reload_scripts"))
		emitter.call("fire")
		emitter.free()
		show("a reload asked for while AS3 runs is refused: the code stays", ask("value"), 4)
		# An untyped field holds a Later; the next version, and the project, no longer have the class.
		ask("holdLater")
		DirAccess.remove_absolute(hot_path().get_base_dir().path_join("later/Later.as"))
		put("v5")
		stage = 5
	elif stage == 5 and waited(1500):
		show("a class gone while an object of it is alive: the game keeps the scripts it had", ask("value"), 4)
		node.free()
		other.free()
		return true
	return false
