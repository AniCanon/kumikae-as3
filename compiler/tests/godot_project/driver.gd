# Attaches Player.as to a body, lets the engine run 30 physics frames, and prints where the body is.
extends SceneTree

var body: CharacterBody2D

func _initialize() -> void:
	var script := load("res://Player.as") as Script
	if script == null or not script.can_instantiate():
		print("RESULT no script")
		quit(1)
		return
	body = CharacterBody2D.new()
	body.set_script(script)
	root.add_child(body)

func _physics_process(_delta: float) -> bool:
	if body == null:
		return true
	if body.steps >= 30:
		print("RESULT steps=%d x=%.3f y=%.3f text=%s" % [body.steps, body.position.x, body.position.y,
				body.describe("moved")])
		# An AS3 error inside a call from the engine is reported, and the engine carries on.
		body.fail()
		print("RESULT after error steps=%d" % body.steps)
		body.queue_free()
		return true
	return false
