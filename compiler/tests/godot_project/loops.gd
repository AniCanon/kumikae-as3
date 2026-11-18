# Loops over Godot's collections in AS3. Each line shows what the AS3 script gave and what GDScript gives
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
	node.set_script(load("res://Loops.as"))
	root.add_child(node)
	for child_name in ["first", "second", "third"]:
		var child := Node.new()
		child.name = child_name
		node.add_child(child)

func described(value) -> String:
	var kind := "object"
	match typeof(value):
		TYPE_INT, TYPE_FLOAT: kind = "number"
		TYPE_STRING, TYPE_STRING_NAME: kind = "string"
		TYPE_BOOL: kind = "boolean"
	return "%s:%s;" % [kind, "null" if value == null else str(value)]

func _physics_process(_delta: float) -> bool:
	frames += 1
	if frames < 2:
		return false
	if frames > 2:
		return true
	var names := ""
	for child in node.get_children():
		names += child.name + ";"
	show("for each over what a method gives", ask("childNames"), names)
	var numbers := [1, 2.5, 4]
	var total := 0.0
	for n in numbers:
		total += n
	show("for each into a Number", ask("sum", [numbers]), total)
	var mixed := [7, "s", true, Vector2(1, 2), null]
	var text := ""
	for value in mixed:
		text += described(value)
	show("for each into an untyped variable", ask("untypedElements", [mixed]), text)
	text = ""
	for i in numbers.size():
		text += "%s=%s;" % [i, str(numbers[i])]
	show("for in gives an Array's indices", ask("indices", [numbers]), text)
	var table := {"a": 1, 2: "two", "c": Vector2(3, 4)}
	text = ""
	for key in table:
		text += described(key)
	show("for in gives a Dictionary's keys", ask("keys", [table]), text)
	var named := {"x": 1, "y": Vector2(3, 4)}
	text = ""
	for key in named:
		text += "%s=%s;" % [key, str(named[key])]
	show("for in into a variable declared above", ask("textKeys", [named]), text)
	text = ""
	for key in table:
		text += str(table[key]) + ";"
	show("for each gives a Dictionary's values", ask("values", [table]), text)
	show("break and continue", ask("untilNegative", [[3, 0, 4, -1, 5]]), 2)
	var growing := [1, 2]
	show("an element added on the way is reached", [ask("grown", [growing]), growing], [3, [1, 2, 5]])
	var shrinking := {"a": 1, "b": 2, "c": 3}
	show("a key erased on the way is passed over, one added is not reached", ask("erasedOnTheWay", [shrinking]), "a;c;")
	show("a loop inside a loop", ask("pairs", [[1, 2, 3]]), "12;13;23;")
	show("for in over a packed array", ask("packedIndices", [PackedInt32Array([5, 6])]), "0=5;1=6;")
	show("in on a Dictionary", [ask("has", [table, "a"]), ask("has", [table, 2]), ask("has", [table, "2"]), ask("has", [table, "z"])],
			["a" in table, 2 in table, "2" in table, "z" in table])
	var smaller := table.duplicate()
	show("delete on a Dictionary", [ask("remove", [smaller, 2]), ask("remove", [smaller, "z"]), smaller], [true, true, {"a": 1, "c": Vector2(3, 4)}])
	show("delete by a name", ask("removeNamed", [{"a": 1, "b": 2, "c": 3}]), 2)
	show("an element of another type", ask("wrongElement", [[node, 5]]), null)
	show("for each into the script's own class", ask("scripted", [[node, node]]), "%s;%s;" % [node.name, node.name])
	var plain := Node2D.new()
	show("a node without the script is not of its class", ask("scripted", [[node, plain]]), null)
	plain.free()
	var points := [Vector2(1, 2), Vector2(3, 4)]
	var area := 0.0
	for point in points:
		area += point.x * point.y
	show("for each into one of Godot's values", ask("points", [points]), area)
	# GDScript refuses a loop over null: AS3 goes through nothing.
	show("no collection is nothing to go through", ask("missing"), "0 in:1009 delete:1009")
	return false
