extends Node2D


func _process(delta: float) -> void:
	rotation += delta
	position.x += 10 * delta
