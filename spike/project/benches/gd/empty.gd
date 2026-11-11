extends Node2D
## Engine-overhead baseline: a script whose _process does nothing.


func _process(_delta: float) -> void:
	pass


func checksum() -> float:
	return 0.0
