# Untyped values between AS3 and Godot. Each line shows what the AS3 script gave and what GDScript gives
# for the same thing; run_godot.py checks the two agree.
extends SceneTree

var node: Node2D
var frames := 0

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func ask(method: String, arguments: Array = []):
	return Callable(node, method).callv(arguments)

func _initialize() -> void:
	node = Node2D.new()
	node.set_script(load("res://Untyped.as"))
	root.add_child(node)

func _physics_process(_delta: float) -> bool:
	frames += 1
	if frames < 2:
		return false
	if frames > 2:
		return true
	show("an int", ask("describe", [5]), "number:5")
	show("a float", ask("describe", [2.5]), "number:2.5")
	show("a whole float", ask("describe", [3.0]), "number:3")
	show("a string", ask("describe", ["hi"]), "string:hi")
	show("a bool", ask("describe", [true]), "boolean:true")
	show("null", ask("describe", [null]), "object:null")
	show("a vector", ask("describe", [Vector2(1, 2)]), "object:(1.0, 2.0)")
	show("a node", ask("describe", [node]).begins_with("object:"), true)
	for value in [7, 7.0, 9007199254740993, "s", Vector2(3, 4), node, null, true]:
		var back = ask("same", [value])
		show("round trip %s" % type_string(typeof(value)), [back, typeof(back)], [value, typeof(value)])
	node.set_meta("whole", 42)
	node.set_meta("fraction", 42.0)
	ask("copyMeta", [node, "whole", "whole2"])
	ask("copyMeta", [node, "fraction", "fraction2"])
	show("meta keeps an int", typeof(node.get_meta("whole2")), TYPE_INT)
	show("meta keeps a float", typeof(node.get_meta("fraction2")), TYPE_FLOAT)
	var table := {"position": Vector2(5, 6), "count": 9}
	show("dictionary element", ask("point", [table]), Vector2(5, 6))
	show("dictionary number", ask("count", [table]), 10)
	show("wrong type", ask("wrongType", [table]), null)
	ask("scriptObject", [node])
	show("script object not given", node.has_meta("mine"), false)
	show("equal values", [ask("equal", [Vector2(1, 1), Vector2(1, 1)]), ask("equal", [5, 5.0]), ask("equal", [node, node])], ["truetrue", "truetrue", "truetrue"])
	return false
