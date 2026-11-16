extends Node2D

const ROUNDS := 20
const SIZE := 1000

var line: Line2D


func _ready() -> void:
	line = Line2D.new()
	add_child(line)


func _process(_delta: float) -> void:
	for k in ROUNDS:
		var points := PackedVector2Array()
		for i in SIZE:
			points.append(Vector2(i, k))
		line.points = points
		var data: Array = []
		for i in SIZE:
			data.append(i + k)
		set_meta(&"data", data)


func checksum() -> float:
	var data: Array = get_meta(&"data")
	return line.get_point_count() + data.size()
