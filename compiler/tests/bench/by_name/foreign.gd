extends Node2D

@export var score := 7
@export var bonus := 9
var health := 11
var speed := 13
var level := 15
var lives := 17
var energy := 19
var armor := 21

func current() -> int:
	return score
