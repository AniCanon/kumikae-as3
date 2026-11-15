# Godot's own classes with `is` and `as` in AS3. Each line shows what the AS3 script gave and what
# GDScript gives for the same thing; run_godot.py checks the two agree.
extends SceneTree

var node: Node2D
var frames := 0

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func ask(method: String, arguments: Array = []):
	return Callable(node, method).callv(arguments)

func kind(event: InputEvent) -> String:
	if event is InputEventScreenDrag:
		return "drag"
	if event is InputEventScreenTouch:
		return "touch"
	if event is InputEventMouse:
		return "mouse"
	return "other"

func _initialize() -> void:
	node = Node2D.new()
	node.set_script(load("res://Events.as"))
	root.add_child(node)

func _process(_delta: float) -> bool:
	frames += 1
	if frames == 2:
		var touch := InputEventScreenTouch.new()
		touch.position = Vector2(10, 20)
		touch.pressed = true
		var drag := InputEventScreenDrag.new()
		drag.position = Vector2(5, 5)
		drag.relative = Vector2(1, 2)
		var click := InputEventMouseButton.new()
		var key := InputEventKey.new()
		for event in [touch, drag, click, key]:
			show("kind of %s" % event.get_class(), ask("kind", [event]), kind(event))
		show("touch position", ask("where", [touch]), Vector2(10, 20))
		show("drag position", ask("where", [drag]), Vector2(6, 7))
		show("neither", ask("where", [key]), Vector2(-1, -1))
		var sprite := Sprite2D.new()
		sprite.name = "Hero"
		show("is Node", [ask("isNode", [sprite]), ask("isNode", [RefCounted.new()])], [true, false])
		show("as Sprite2D", [ask("asSprite", [sprite]), ask("asSprite", [node])], ["Hero", "not a sprite"])
		sprite.free()
		show("null", ask("nothing"), "falsetrue")
		var up := InputEventScreenTouch.new()
		up.position = Vector2(30, 0)
		Input.parse_input_event(touch)
		Input.parse_input_event(drag)
		Input.parse_input_event(up)
	if frames == 4:
		show("delivered", ask("heard"), "down10 drag up30 ")
		return true
	return false
