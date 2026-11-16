extends Node2D
## Holdout: allocation every frame.

const COUNT := 20000


class Particle:
	var x: float
	var y: float
	var vx: float
	var vy: float

	func _init(px: float, py: float, pvx: float, pvy: float) -> void:
		x = px
		y = py
		vx = pvx
		vy = pvy


var slots: Array[Particle] = []
var frame := 0
var last := 0.0


func _ready() -> void:
	slots.resize(COUNT)


func _process(_delta: float) -> void:
	frame += 1
	for i in COUNT:
		slots[i] = Particle.new(i, frame, i * 0.5, frame * 0.25)
	var sum := 0.0
	for i in COUNT:
		var p := slots[i]
		sum += p.x + p.vy
	last = sum


func checksum() -> float:
	return last
