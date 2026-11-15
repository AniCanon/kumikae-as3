# [Export] in the editor (run with --editor, in a copy of the project): a field exported by an edit to the
# script shows on the object being edited without a restart, and a value set before the edit stays.
extends SceneTree

var node: Node2D
var started := 0

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
	started = Time.get_ticks_msec()

func _process(_delta: float) -> bool:
	if Time.get_ticks_msec() - started < 2500:
		return false
	show("the new field", node.get("added"), 9)
	show("a value set before stays", node.get("speed"), 8.0)
	node.free()
	return true
