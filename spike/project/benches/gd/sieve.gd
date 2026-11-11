extends Node2D

const LIMIT := 200000

var flags := PackedInt32Array()
var total := 0


func _ready() -> void:
	flags.resize(LIMIT)


func _process(_delta: float) -> void:
	for i in LIMIT:
		flags[i] = 1
	var count := 0
	for i in range(2, LIMIT):
		if flags[i] == 1:
			count += 1
			var j := i + i
			while j < LIMIT:
				flags[j] = 0
				j += i
	total += count


func checksum() -> float:
	return total
