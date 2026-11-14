# Int64 at Godot's edge. Each line shows what the AS3 script gave and what GDScript gives for the same
# thing; run_godot.py checks the two agree.
extends SceneTree

var node: Node2D
var frames := 0

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func ask(method: String, arguments: Array = []):
	return Callable(node, method).callv(arguments)

func _initialize() -> void:
	node = Node2D.new()
	node.set_script(load("res://Wide.as"))
	root.add_child(node)

func _physics_process(_delta: float) -> bool:
	frames += 1
	if frames < 2:
		return false
	if frames > 2:
		return true
	var big := 9007199254740993
	var most := 9223372036854775807
	# What Godot gives back.
	var counted := RefCounted.new()
	show("counted id", ask("idOf", [counted]), counted.get_instance_id())
	show("node id", ask("idOf", [node]), node.get_instance_id())
	show("same id", ask("sameId", [counted, counted.get_instance_id()]), true)
	show("rgba64", ask("rgba64", [Color(1, 0.5, 0.25, 1)]), Color(1, 0.5, 0.25, 1).to_rgba64())
	show("rgba32", ask("rgba32", [Color(1, 0.5, 0.25, 1)]), Color(1, 0.5, 0.25, 1).to_rgba32())
	show("count", ask("count", [[1, 2, 3]]), 3)
	show("frames", ask("frames"), true)
	var wide := PackedInt64Array([big, -most])
	show("element", ask("element", [wide, 0]), big)
	show("negative element", ask("element", [wide, 1]), -most)
	show("plus one", ask("plusOne", [wide]), big + 1)
	show("stored", ask("stored", [PackedInt64Array([0]), most]), PackedInt64Array([most - 1, most]))
	show("narrow fits", ask("narrow", [PackedInt32Array(), 7]), PackedInt32Array([7]))
	show("narrow refused", ask("narrow", [PackedInt32Array(), 5000000000]), null)
	# What GDScript gives.
	show("echo most", ask("echo", [most]), most)
	show("echo least", ask("echo", [-most - 1]), -most - 1)
	show("wraps", ask("next", [most]), -most - 1)
	show("whole float", ask("echo", [3.0]), 3)
	show("fraction refused", ask("echo", [2.5]), null)
	show("text refused", ask("echo", ["5"]), null)
	show("int refused", ask("asInt", [5000000000]), null)
	show("number exact", ask("asNumber", [9007199254740992]), 9007199254740992.0)
	show("number refused", ask("asNumber", [big]), null)
	show("to number", ask("toNumber", [4]), 4.0)
	show("to number refused", ask("toNumber", [big]), null)
	show("field", node.kept, big)
	show("flags", ask("flags"), RenderingServer.ARRAY_FLAG_COMPRESS_ATTRIBUTES)
	# Whole numbers worked out in AS3 reach Godot whole.
	show("product", ask("product", [Vector2i(1, 2), 3, 5]), Vector2i(1, 2) * 15)
	show("product past int", ask("product", [Vector2i(1, 1), 50000, 60000]), null)
	# A type that stores 32-bit integers never sees a wide one.
	var past := 4294967299
	show("cell", ask("cell", [7]), Vector2i(7, 0))
	show("cell refused", ask("cell", [past]), null)
	show("cell3 refused", ask("cell3", [past]), null)
	show("box", ask("box", [3]), Rect2i(3, 3, 5, 5))
	show("box refused", ask("box", [past]), null)
	show("over", ask("over", [Vector2i(100, 100), 4]), Vector2i(100, 100) / 4)
	show("over refused", ask("over", [Vector2i(100, 100), past]), null)
	show("remainder", ask("remainder", [Vector2i(100, 100), 7]), Vector2i(100, 100) % 7)
	show("remainder refused", ask("remainder", [Vector2i(100, 100), past]), null)
	show("times", ask("times", [Vector2i(1, 2), 3]), Vector2i(1, 2) * 3)
	show("times refused", ask("times", [Vector2i(1, 2), past]), null)
	show("times left refused", ask("timesLeft", [Vector2i(1, 2), past]), null)
	show("grow refused", ask("grow", [Vector2i(1, 2), past]), null)
	show("at least one", ask("atLeastOne", [[]]), 1)
	show("at least its size", ask("atLeastOne", [[4, 5]]), 2)
	# A listener's argument.
	ask("listen")
	Input.joy_connection_changed.emit(5000000000, true)
	show("heard", node.heard, 5000000000)
	ask("unlisten")
	node.free()
	return false
