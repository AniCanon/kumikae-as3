extends SceneTree

# Runs one workload of CmpAs.as, small: run_godot.py holds what the native code leaves to the library
# meanwhile to native_counts.txt. `-- <workload> <rounds>`.


func _initialize() -> void:
	var asked := OS.get_cmdline_user_args()
	var node := Node2D.new()
	node.set_script(load("res://CmpAs.as"))
	root.add_child(node)
	print("COUNTED ", asked[0], " ", node.call(asked[0], int(asked[1])))


func _process(_delta: float) -> bool:
	return true
