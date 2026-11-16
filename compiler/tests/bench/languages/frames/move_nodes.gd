extends Node2D

const COUNT := 20000

var children: Array[Node2D] = []
var velocity := Vector2(60, 30)


func _ready() -> void:
	for i in COUNT:
		var child := Node2D.new()
		child.position = Vector2(i % 1000, i / 1000)
		add_child(child)
		children.append(child)


func _process(delta: float) -> void:
	var n := children.size()
	for i in n:
		var child := children[i]
		var p := child.position
		p = p + velocity * delta
		if p.x > 1000:
			p.x -= 1000
		child.position = p


func checksum() -> float:
	var sum := 0.0
	for child in children:
		sum += child.position.x
	return sum
