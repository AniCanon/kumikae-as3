extends Node2D
## Holdout: objects and overridable method calls.

const COUNT := 9000
const PASSES := 10


class Shape:
	func area() -> float:
		return 0.0

	func grow(_d: float) -> void:
		pass


class Circle extends Shape:
	var r: float

	func _init(pr: float) -> void:
		r = pr

	func area() -> float:
		return PI * r * r

	func grow(d: float) -> void:
		r += d


class Rect extends Shape:
	var w: float
	var h: float

	func _init(pw: float, ph: float) -> void:
		w = pw
		h = ph

	func area() -> float:
		return w * h

	func grow(d: float) -> void:
		w += d
		h += d


class Tri extends Shape:
	var b: float
	var h: float

	func _init(pb: float, ph: float) -> void:
		b = pb
		h = ph

	func area() -> float:
		return 0.5 * b * h

	func grow(d: float) -> void:
		b += d


var shapes: Array[Shape] = []
var total := 0.0


func _ready() -> void:
	for i in COUNT:
		var size := 1.0 + i * 0.001
		match i % 3:
			0:
				shapes.append(Circle.new(size))
			1:
				shapes.append(Rect.new(size, 2.0))
			_:
				shapes.append(Tri.new(3.0, size))


func _process(_delta: float) -> void:
	var n := shapes.size()
	for p in PASSES:
		for i in n:
			var s := shapes[i]
			s.grow(0.001)
			total += s.area()


func checksum() -> float:
	return total
