# Godot's global functions in AS3. Each line shows what the AS3 script gave and what GDScript gives for
# the same thing; run_godot.py checks the two agree.
extends SceneTree

var node: Node2D
var frames := 0

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func ask(method: String, arguments: Array = []):
	return Callable(node, method).callv(arguments)

func _initialize() -> void:
	node = Node2D.new()
	node.set_script(load("res://Functions.as"))
	root.add_child(node)

func _physics_process(_delta: float) -> bool:
	frames += 1
	if frames < 2:
		return false
	if frames > 2:
		return true
	show("lerpf", ask("mix", [2.0, 6.0, 0.25]), lerpf(2.0, 6.0, 0.25))
	show("clampf", ask("hold", [3.5]), clampf(3.5, -1, 1))
	show("clampi", ask("holdWhole", [42]), clampi(42, 0, 10))
	show("absi", ask("size", [-9007199254740993]), absi(-9007199254740993))
	show("maxi", ask("larger", [3, 7]), maxi(3, 7))
	show("sin", ask("wave", [30.0]), sin(deg_to_rad(30.0)))
	show("snappedf", ask("snap", [0.6]), snappedf(0.6, 0.25))
	show("wrapi", ask("wrap", [7]), wrapi(7, 0, 5))
	show("posmod", ask("remainder", [-7, 3]), posmod(-7, 3))
	show("is_equal_approx", ask("close", [0.1 + 0.2, 0.3]), is_equal_approx(0.1 + 0.2, 0.3))
	show("randf", ask("chance"), true)
	show("randi_range", ask("roll"), true)
	show("floori and roundi", ask("whole", [2.6]), floori(2.6) + roundi(2.6))
	show("hash", ask("mark", ["abc"]), hash("abc"))
	show("str", ask("join", [1, "a", Vector2(2, 3)]), str(1, "a", Vector2(2, 3)))
	show("is_instance_valid", ask("alive", [node]), true)
	show("instance_from_id", ask("find", [node.get_instance_id()]) == node, true)
	var counted := RefCounted.new()
	show("instance_from_id counted", ask("find", [counted.get_instance_id()]) == counted, true)
	ask("say", [4, Vector2(1, 2)])
	show("lerp as a number", ask("mixAny", [2.0, 6.0, 0.25]), lerp(2.0, 6.0, 0.25))
	show("max of three", ask("largest", [3, 9, 7]), max(3, 9, 7))
	show("wrong argument", ask("wrong", ["one"]), null)
	show("in a loop", ask("count", [10]), 3 + 3 + 3 + 3 + 4 + 5 + 6 + 7 + 8 + 9)
	node.free()
	return false
