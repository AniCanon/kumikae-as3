# [Export] in the editor (run with --editor, in a copy of the project): a field exported by an edit to the
# script shows on the object being edited without a restart, and a value set before the edit stays.
extends SceneTree

var node: Node2D
var mark := 0 # The watch's looks when the script was saved.

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
	node = Node2D.new()
	node.set_script(load("res://Exported.as"))
	node.set("speed", 8.0)
	var text := FileAccess.get_file_as_string("res://Exported.as")
	text = text.replace("public var notExported:int = 4;", "public var notExported:int = 4;\n\t\t[Export] public var added:int = 9;")
	var file := FileAccess.open("res://Exported.as", FileAccess.WRITE)
	file.store_string(text)
	file.close()
	mark = looks()

func _process(_delta: float) -> bool:
	# The watch acts on the edit by its second look; the object being edited has the field soon after.
	if node.get("added") == null and looks() < mark + 12:
		return false
	show("the new field", node.get("added"), 9)
	show("a value set before stays", node.get("speed"), 8.0)
	node.free()
	return true
