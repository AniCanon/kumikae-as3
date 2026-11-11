extends Node2D
## Holdout: a unit-stride numeric loop.

const N := 100000
const PASSES := 10
const A := 0.000001

var x := PackedFloat64Array()
var y := PackedFloat64Array()
var last := 0.0


func _ready() -> void:
	x.resize(N)
	y.resize(N)
	for i in N:
		x[i] = i * 0.5
		y[i] = i


func _process(_delta: float) -> void:
	for p in PASSES:
		for i in N:
			y[i] = A * x[i] + y[i]
	var sum := 0.0
	for i in N:
		sum += y[i]
	last = sum


func checksum() -> float:
	return last
