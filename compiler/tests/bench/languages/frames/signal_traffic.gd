extends Node2D

signal hit(amount: int, pos: Vector2)

const EMITS := 20000

var total := 0.0


func _ready() -> void:
	hit.connect(on_hit)


func _process(_delta: float) -> void:
	for i in EMITS:
		hit.emit(i, Vector2(i, 1))


func on_hit(amount: int, pos: Vector2) -> void:
	total += amount + pos.x


func checksum() -> float:
	return total
