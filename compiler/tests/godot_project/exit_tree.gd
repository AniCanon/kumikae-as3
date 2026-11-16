# Godot closing: what AS3 does as the scene tree is taken down still works. run_godot.py reads the output.
extends SceneTree

func _initialize() -> void:
	var node := Node.new()
	node.set_script(load("res://Late.as"))
	root.add_child(node)

func _process(_delta: float) -> bool:
	return true
