extends Node2D

const STEPS := 2000
const SOLAR_MASS := 4 * PI * PI
const YEAR := 365.24


class Body:
	var x: float
	var y: float
	var z: float
	var vx: float
	var vy: float
	var vz: float
	var mass: float

	func _init(px: float, py: float, pz: float, pvx: float, pvy: float, pvz: float, pmass: float) -> void:
		x = px
		y = py
		z = pz
		vx = pvx
		vy = pvy
		vz = pvz
		mass = pmass


var bodies: Array[Body] = []


func _ready() -> void:
	bodies.append(Body.new(0, 0, 0, 0, 0, 0, SOLAR_MASS))
	bodies.append(Body.new(4.84143144246472090e+00, -1.16032004402742839e+00, -1.03622044471123109e-01,
			1.66007664274403694e-03 * YEAR, 7.69901118419740425e-03 * YEAR, -6.90460016972063023e-05 * YEAR,
			9.54791938424326609e-04 * SOLAR_MASS))
	bodies.append(Body.new(8.34336671824457987e+00, 4.12479856412430479e+00, -4.03523417114321381e-01,
			-2.76742510726862411e-03 * YEAR, 4.99852801234917238e-03 * YEAR, 2.30417297573763929e-05 * YEAR,
			2.85885980666130812e-04 * SOLAR_MASS))
	bodies.append(Body.new(1.28943695621391310e+01, -1.51111514016986312e+01, -2.23307578892655734e-01,
			2.96460137564761618e-03 * YEAR, 2.37847173959480950e-03 * YEAR, -2.96589568540237556e-05 * YEAR,
			4.36624404335156298e-05 * SOLAR_MASS))
	bodies.append(Body.new(1.53796971148509165e+01, -2.59193146099879641e+01, 1.79258772950371181e-01,
			2.68067772490389322e-03 * YEAR, 1.62824170038242295e-03 * YEAR, -9.51592254519715870e-05 * YEAR,
			5.15138902046611451e-05 * SOLAR_MASS))
	var px := 0.0
	var py := 0.0
	var pz := 0.0
	for b in bodies:
		px += b.vx * b.mass
		py += b.vy * b.mass
		pz += b.vz * b.mass
	bodies[0].vx = -px / SOLAR_MASS
	bodies[0].vy = -py / SOLAR_MASS
	bodies[0].vz = -pz / SOLAR_MASS


func _process(_delta: float) -> void:
	for step in STEPS:
		advance(0.01)


func advance(dt: float) -> void:
	var n := bodies.size()
	for i in n:
		var bi := bodies[i]
		for j in range(i + 1, n):
			var bj := bodies[j]
			var dx := bi.x - bj.x
			var dy := bi.y - bj.y
			var dz := bi.z - bj.z
			var d2 := dx * dx + dy * dy + dz * dz
			var mag := dt / (d2 * sqrt(d2))
			bi.vx -= dx * bj.mass * mag
			bi.vy -= dy * bj.mass * mag
			bi.vz -= dz * bj.mass * mag
			bj.vx += dx * bi.mass * mag
			bj.vy += dy * bi.mass * mag
			bj.vz += dz * bi.mass * mag
	for i in n:
		var b := bodies[i]
		b.x += dt * b.vx
		b.y += dt * b.vy
		b.z += dt * b.vz


func checksum() -> float:
	var e := 0.0
	var n := bodies.size()
	for i in n:
		var bi := bodies[i]
		e += 0.5 * bi.mass * (bi.vx * bi.vx + bi.vy * bi.vy + bi.vz * bi.vz)
		for j in range(i + 1, n):
			var bj := bodies[j]
			var dx := bi.x - bj.x
			var dy := bi.y - bj.y
			var dz := bi.z - bj.z
			e -= (bi.mass * bj.mass) / sqrt(dx * dx + dy * dy + dz * dz)
	return e
