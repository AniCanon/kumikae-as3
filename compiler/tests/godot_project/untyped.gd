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
	show("a string name", ask("describe", [&"name"]), "string:name")
	show("a node path", ask("describe", [^"a/b"]), "string:a/b")
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
	var other := Node2D.new()
	show("unequal values", [ask("equal", [Vector2(1, 1), Vector2(1, 2)]), ask("equal", [node, other])], ["falsefalse", "falsefalse"])
	other.free()
	show("large integers", [ask("order", [9007199254740992, 9007199254740993]), ask("order", [9223372036854775806, 9223372036854775807])],
			["falsefalsetruetrue", "falsefalsetruetrue"])
	show("a vector stays a vector", ask("asVector", [Vector2(1, 2)]), Vector2(1, 2))
	show("a color is not a vector", ask("asVector", [Color(1, 0.5, 0.25)]), null)
	show("a vector3 is not a vector2", ask("asVector", [Vector3(3, 4, 12)]), null)
	show("a resource is not a node", ask("asNode", [Resource.new()]), null)
	show("a node is a node", ask("asNode", [node]) == node, true)
	show("a property takes its type only", [ask("place", [Vector2(7, 8)]), ask("place", [Color(1, 1, 1)])], [Vector2(7, 8), null])
	show("a script object returned", ask("giveScript"), null)
	show("a script object in a field", node.get("keep"), null)
	show("freed", ask("freed"), "truetruetruenotruetrue")
	ask("throwValue", [Vector2(3, 4)])
	node.position = Vector2(1, 1)
	show("an int plus one stays an int", [ask("increment", [5]), typeof(ask("increment", [5]))], [6, TYPE_INT])
	show("a Godot value and an untyped one", ask("moveBy", [Vector2(2, 3)]), Vector2(3, 4))
	show("a value read above its line is its zero", ask("readEarly", []), "(0.0, 0.0) (0.0, 0.0) (0.0, 0.0) (0.0, 0.0)")
	show("an untyped node is a Node", [ask("holdsNode", [node]), ask("holdsNode2D", [node]), ask("holdsNode", [Vector2.ONE]), ask("holdsNode", [5]), ask("holdsNode", [null])], [true, true, false, false, false])
	show("an untyped value as a Node", [ask("asNodeOrNull", [node]) == node, ask("asNodeOrNull", [5]), ask("asNodeOrNull", ["text"])], [true, null, null])
	show("the script's own object held untyped is its node", ask("selfHeld", []),
			[node is Node, node is Node2D, node is Sprite2D, (node as Node) != null, true, true, true, true, true, true, false, true])
	var one := Node2D.new()
	var two := Node2D.new()
	show("a part is written to the object named before the value", ask("writeOrder", [one, two]), Vector2(5, 0))
	show("the other object is untouched", [two.position.x, two.visible], [0.0, true])
	one.free()
	two.free()
	for method in ["minusOne", "twice", "stepped"]:
		var got = ask(method, [5])
		show("%s of an int stays an int" % method, typeof(got), TYPE_INT)
	show("an Int64 minus one stays whole", [ask("minusOne", [5000000000]), typeof(ask("minusOne", [5000000000]))], [4999999999, TYPE_INT])
	show("a half is a float", ask("half", [5]), 2.5)
	show("a Godot value plus text joins them", ask("plusPosition", ["text"]), "(1.0, 1.0)text")
	show("text plus a Godot value joins them", ask("positionAfter", ["text"]), "text(1.0, 1.0)")
	show("text plus-assigned a Godot value joins them", ask("addPosition", ["text"]), "text(1.0, 1.0)")
	show("a Godot value plus a number joins them, as AS3 does", ask("plusPosition", [2]), "(1.0, 1.0)2")
	show("a Godot value plus-assigned a Godot value", ask("addPosition", [Vector2(1, 2)]), Vector2(2, 3))
	show("untyped Godot values multiplied, subtracted, divided and negated",
		[ask("times", [Vector2(1, 2), 3]), ask("times", [3, Vector2(1, 2)]), ask("times", [Vector2(1, 2), Vector2(3, 4)]), ask("minus", [Vector2(3, 4), Vector2(1, 1)]),
			ask("over", [Vector2(4, 2), 2]), ask("remainder", [Vector2i(7, 5), Vector2i(4, 3)]), ask("negated", [Vector2(1, -2)]),
			ask("times", [Vector2i(1, 2), 3]), ask("times", [Vector2i(1, 2), 1.5])],
		[Vector2(1, 2) * 3, 3 * Vector2(1, 2), Vector2(1, 2) * Vector2(3, 4), Vector2(3, 4) - Vector2(1, 1), Vector2(4, 2) / 2, Vector2i(7, 5) % Vector2i(4, 3), -Vector2(1, -2),
			Vector2i(1, 2) * 3, Vector2i(1, 2) * 1.5])
	show("untyped Godot values in order", [ask("ordered", [Vector2(1, 2), Vector2(1, 3)]), ask("ordered", [Vector2(2, 0), Vector2(1, 3)]), ask("ordered", [Vector2(1, 2), Vector2(1, 2)])],
		["truetruefalsefalse", "falsefalsetruetrue", "falsetruefalsetrue"])
	show("an untyped Godot value multiplied and subtracted in place", ask("scaled", [Vector2(1, 2), Vector2(3, 4)]), Vector2(1, 2) * Vector2(3, 4) - Vector2(3, 4))
	show("a typed Godot value times an untyped one", [ask("positionTimes", [2]), ask("positionTimes", [Vector2(2, 3)])], [node.position * 2, node.position * Vector2(2, 3)])
	show("untyped numbers stay AS3's", [ask("times", [3, 4]), typeof(ask("times", [3, 4])), ask("minus", ["7", 2]), ask("over", [1, 4]), ask("remainder", [7, 4]), ask("negated", ["3"])],
		[12, TYPE_INT, 5, 0.25, 3, -3])
	show("where Godot has no such operator the answer is AS3's", ask("timesText", [Vector2(1, 2), "x"]), "NaN NaN (-1.0, -2.0)")
	show("caught in AS3", [ask("guarded", [Vector2(2, 3)]), ask("guarded", [Color(1, 1, 1)]), node.position], ["fits (2.0, 3.0)", "caught true", Vector2(1, 1)])
	var thrower := Node.new()
	thrower.set_script(load("res://Thrower.as"))
	show("a toString() that throws, caught where the text was asked for", ask("caughtText", [thrower]), "caught from toString")
	thrower.free()
	var kinds := []
	var as_text := []
	var as_wanted := []
	for value in [Vector2(1, 2), Color(1, 0, 0), Vector2i(3, 4), [1], PackedInt32Array([1]), {"k": 1}, 5, "text", node, null]:
		kinds.append([ask("isValue", [value]), [value is Vector2, value is Color, value is Vector2i, value is Array, value is PackedInt32Array]])
		as_text.append(ask("asValue", [value]))
		as_wanted.append("%s/%s/%s" % [str(value) if value is Vector2 else "null", str(value) if value is Color else "null", str(value) if value is Dictionary else "null"])
	show("is with Godot's value types", kinds.map(func(pair): return pair[0]), kinds.map(func(pair): return pair[1]))
	show("as with Godot's value types", as_text, as_wanted)
	# GDScript has no cast that refuses: what AS3's gives is written out.
	show("a cast to one of Godot's value types", [ask("castValue", [Vector2(5, 6)]), ask("castValue", [null]), ask("castValue", [Vector2i(1, 2)]), ask("castValue", ["text"])],
			[Vector2(5, 6), null, null, null])
	show("is, as and a cast on typed values", ask("typedValue", [Vector2(1, 2), Color(1, 0, 0)]),
			"true/false/%s/null/false/null/false/false/%s/null/false" % [str(Color(1, 0, 0)), str(Vector2(1, 2))])
	var refused := "refused: JSON.stringify can't write one of Godot's values or objects; convert it to an AS3 value first"
	show("JSON refuses Godot's values", [ask("json", [Vector2(1, 2)]), ask("json", [node]), ask("json", [5]), ask("json", ["a"])],
		[refused, refused, "{\"p\":[1,5]}", "{\"p\":[1,\"a\"]}"])
	return false
