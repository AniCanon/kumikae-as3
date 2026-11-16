extends Node2D

const RAYS := 2000

var params: PhysicsRayQueryParameters2D
var hits := 0
var sum_x := 0.0


func _ready() -> void:
	for gy in 8:
		for gx in 8:
			var body := StaticBody2D.new()
			var shape := CollisionShape2D.new()
			var circle := CircleShape2D.new()
			circle.radius = 20
			shape.shape = circle
			body.add_child(shape)
			body.position = Vector2(gx * 100 + 50, gy * 100 + 50)
			add_child(body)
	params = PhysicsRayQueryParameters2D.new()


func _physics_process(_delta: float) -> void:
	var space := get_world_2d().direct_space_state
	for i in RAYS:
		params.from = Vector2(-50, i % 800)
		params.to = Vector2(850, (i * 7) % 800)
		var result := space.intersect_ray(params)
		if not result.is_empty():
			hits += 1
			var pos: Vector2 = result["position"]
			sum_x += pos.x


func checksum() -> float:
	return hits + sum_x
