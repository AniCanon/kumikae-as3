extends Node2D
var total: float = 0
var steps: int = 0
func spin(rounds: int) -> float:
	var step := Vector2(0.5, 0.25)
	var pressed := 0
	for i in rounds:
		position = position + step
		rotation += 0.001
		if is_inside_tree():
			pressed += 1
		if Input.is_action_pressed("ui_accept"):
			pressed -= 1
	return position.x + pressed
func work(n: int) -> int:
	var checksum := 0
	for i in n:
		total += scaled(i, 0.5)
		if (i & 1023) == 0:
			checksum ^= bump(i)
	return checksum
func scaled(value: int, by: float) -> float:
	return value * by + 1
func bump(value: int) -> int:
	steps += 1
	return value % 97 + steps
func vec(n: int) -> float:
	var v := Vector2.ZERO
	var d := Vector2(0.5, 0.25)
	for i in n:
		v = v + d * 0.5
		v = v.normalized() * 3.0
	return v.x
