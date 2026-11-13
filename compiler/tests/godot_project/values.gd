# Godot's values in AS3. Each line shows what the AS3 script gave and what GDScript gives for the same
# thing; run_godot.py checks the two agree.
extends SceneTree

var node: Node2D
var frames := 0

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func ask(method: String, arguments: Array):
	return Callable(node, method).callv(arguments)

func _initialize() -> void:
	node = Node2D.new()
	node.set_script(load("res://Values.as"))
	node.position = Vector2(10, 20)
	root.add_child(node)

func _physics_process(_delta: float) -> bool:
	frames += 1
	if frames < 2:
		return false
	if frames > 2:
		return true
	var a := Vector2(1.5, -2)
	var b := Vector2(4, 0.25)
	var cell := Vector2i(7, -3)
	# Operators.
	show("add", ask("add", [a, b]), a + b)
	show("sub", ask("sub", [a, b]), a - b)
	show("times", ask("times", [a, 2.5]), a * 2.5)
	show("times int", ask("scaleInt", [a, 3]), a * 3)
	show("number first", ask("scaleLeft", [2.5, a]), 2.5 * a)
	show("divide", ask("divide", [a, 4.0]), a / 4.0)
	show("negate", ask("negate", [a]), -a)
	show("cells", ask("cells", [cell, 2]), cell * 2)
	show("cells by number", ask("cellsNumber", [cell, 0.5]), cell * 0.5)
	show("cells over", ask("cellsOver", [cell, 2]), cell / 2)
	show("tint", ask("tint", [Color(1, 0.5, 0.25), Color(0.5, 0.5, 1)]), Color(1, 0.5, 0.25) * Color(0.5, 0.5, 1))
	show("same", ask("same", [a, Vector2(1.5, -2)]), a == Vector2(1.5, -2))
	show("not same", ask("same", [a, b]), a == b)
	show("differs", ask("differs", [a, b]), a != b)
	show("same node", ask("sameNode", [node, node]), true)
	show("not same node", ask("sameNode", [node, root]), false)
	show("other node", ask("otherNode", [node, root]), true)
	show("chain", ask("chain", [a, b]), (a + b) * 2 - a / 2)
	show("compound", ask("compound", [a, b]), (a + b) * 3)
	show("moved", ask("moved", [a]), node.position + a)
	# Godot refuses to divide whole numbers by zero: an AS3 error, and the script carries on.
	show("cells over zero", ask("cellsOver", [cell, 0]), null)
	show("after the error", ask("add", [a, a]), a + a)
	# Text: Godot's own for its values and objects.
	show("text", ask("text", [a]), "at " + str(a))
	show("text after", ask("textAfter", [a]), str(a) + " here")
	show("cast", ask("cast", [a]), str(a))
	show("own", ask("own", [a]), str(a))
	show("color text", ask("colorText", [Color(1, 0.5, 0.25)]), str(Color(1, 0.5, 0.25)))
	show("node text", ask("nodeText", [root]), str(root))
	show("node joined", ask("nodeJoined", [root]), "node " + str(root))
	show("null text", ask("nullText", []), "v=null")
	show("null node", ask("nullNode", [null]), "null")
	show("null own", ask("nullOwn", [null]), null)
	ask("traced", [a])
	print("EXPECT TRACE %s %s" % [str(a), str(a.x)])
	var gone := Node.new()
	ask("keep", [gone])
	gone.free()
	show("freed text", ask("keptText", []), "kept " + str(gone))
	node.queue_free()
	return true
