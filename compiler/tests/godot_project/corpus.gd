# The corpus programs that are said to run, run: each is put on a node of the class it extends, in the
# tree, for some frames. run_godot.py copies them into a copy of this project and reads what is printed.
extends SceneTree

var names: PackedStringArray
var at := -1
var node: Node
var frames := 0

func _initialize() -> void:
	names = OS.get_cmdline_user_args()

func _process(_delta: float) -> bool:
	if node == null:
		at += 1
		if at >= names.size():
			return true
		var script = load("res://%s.as" % names[at])
		node = ClassDB.instantiate(script.get_instance_base_type()) if script else null
		if node == null:
			print("NOT LOADED %s" % names[at])
			return false
		node.set_script(script)
		root.add_child(node)
		frames = 0
		return false
	frames += 1
	if frames >= 10:
		print("RAN %s" % names[at])
		node.free()
		node = null
	return false
