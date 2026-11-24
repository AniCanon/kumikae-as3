extends Node2D

var score := 7
var caption := "ready"
var heading := Vector2(3, 4)

func _ready() -> void:
	score = 7

func current() -> int:
	return score

func _get(property: StringName):
	return 11 if property == &"virtual_score" else null
