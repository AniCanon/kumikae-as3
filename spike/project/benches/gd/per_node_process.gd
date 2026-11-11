extends Node2D

const COUNT := 20000
const CHILD := preload("res://benches/gd/per_node_child.gd")

var children: Array[Node2D] = []


func _ready() -> void:
	for i in COUNT:
		var child := Node2D.new()
		child.set_script(CHILD)
		add_child(child)
		children.append(child)


func checksum() -> float:
	var sum := 0.0
	for child in children:
		sum += child.rotation + child.position.x
	return sum
