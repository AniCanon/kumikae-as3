extends Node2D

var score := 7

func _get(property: StringName):
	return 11 if property == &"virtual_score" else null
