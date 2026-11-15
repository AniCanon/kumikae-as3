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
	show("freed text", ask("keptText", []), "kept " + str(gone))
	show("alive is not null", ask("keptIsNull", []), false)
	show("alive is set", ask("keptIsSet", []), true)
	show("alive counts", ask("keptCounts", []), true)
	gone.free()
	show("freed text after", ask("keptText", []), "kept " + str(gone))
	show("freed is null", ask("keptIsNull", []), true)
	show("freed is not set", ask("keptIsSet", []), false)
	show("freed does not count", ask("keptCounts", []), false)
	show("not freed", ask("keptNot", []), true)
	show("freed name", ask("keptName", []), null)
	ask("keptRename", [])
	show("freed children", ask("keptChildren", []), null)
	ask("keptSignal", [])
	ask("keptByName", [])
	show("freed as an argument", ask("keptUnder", [node]), false)
	print("AFTER FREED")
	# One part of a value. The twin has the same done to it in GDScript.
	var twin := Node2D.new()
	twin.position = node.position
	twin.position.x = 7
	show("set x", ask("setX", [7.0]), twin.position)
	twin.position.x += 2.5
	show("add x", ask("addX", [2.5]), twin.position)
	twin.position.x += 1
	show("bump x", ask("bumpX", []), twin.position)
	twin.transform.origin.y = -4
	show("nested", ask("nested", [-4.0]), twin.position)
	show("held part", ask("heldPart", [9.0]), Vector2(1, 9))
	show("local part", ask("localPart", [a, 6.0]), Vector2(6, a.y) + a)
	twin.position.y = 3
	show("other part", [ask("otherPart", [3.0]), node.position], [1, twin.position])
	twin.position.x = twin.position.y
	show("from itself", ask("fromItself", []), twin.position)
	# By index.
	show("first", ask("first", [a]), a[0])
	show("color part", ask("colorPart", [Color(1, 0.5, 0.25)]), Color(1, 0.5, 0.25)[1])
	show("set second", ask("setSecond", [a, 9.0]), Vector2(a.x, 9))
	show("add element", ask("addElement", [a, 2.0]), Vector2(a.x + 2, a.y))
	twin.position[0] = -8
	show("position element", ask("positionElement", [-8.0]), twin.position)
	var numbers := PackedInt32Array([4, 5, 6])
	show("packed", ask("packed", [numbers, 2]), numbers[2])
	var changed := PackedInt32Array([4, 77, 6])
	show("set packed", ask("setPacked", [PackedInt32Array([4, 5, 6]), 1, 77]), changed)
	show("packed out of range", ask("packed", [numbers, 3]), null)
	show("out of range", ask("outOfRange", [a]), null)
	# Arguments left out.
	var c := Color(2, -1, 0.5, 0.5)
	var low := Color(0.25, 0.25, 0.25, 1)
	show("clamp default", ask("clampDefault", [c]), c.clamp())
	show("clamp one", ask("clampOne", [c]), c.clamp(low))
	show("clamp trailing null", ask("clampTrailingNull", [c]), c.clamp(low))
	show("clamp both null", ask("clampBothNull", [c]), c.clamp())
	show("clamp middle null", ask("clampMiddleNull", [c]), c.clamp(Color(0, 0, 0, 0), Color(0.5, 0.5, 0.5, 1)))
	show("clamp null variable", ask("clampNullVariable", [c]), c.clamp(Color(0, 0, 0, 0), Color(0.5, 0.5, 0.5, 1)))
	var t := Transform3D(Basis(), Vector3(1, 2, 3))
	show("looking", ask("looking", [t, Vector3(5, 0, 5)]), t.looking_at(Vector3(5, 0, 5)))
	show("looking null", ask("lookingNull", [t, Vector3(5, 0, 5)]), t.looking_at(Vector3(5, 0, 5)))
	show("whole numbers", ask("wholeNumbers", []), Color(1, 0, 1))
	# toString(): a script's own is what Godot shows; without one, Godot's own text stands.
	var named := Node.new()
	named.set_script(load("res://Named.as"))
	show("to string", str(named), "Named(the one)")
	show("to string in AS3", named.describe(), "I am Named(the one)")
	named.free()
	show("no to string", str(node).contains("<Node2D#"), true)
	show("no to string in AS3", ask("describe", []), "I am [object Values]")
	# What the review of this slice found.
	show("negative cells", ask("negCells", [cell]), cell * -1)
	show("cells plus", ask("cellsPlus", [cell, 2]), cell * 3)
	show("cells compound", ask("cellsCompound", [cell, 2]), cell * 3)
	twin.position = Vector2(1, 2)
	twin.position.x = (func(): twin.position = Vector2(100, 200); return 1).call()
	show("plain order", ask("plainOrder", []), twin.position)
	show("compound order", ask("compoundOrder", []), Vector2(2, 2))
	show("mixed", ask("mixed", [a, root]), false)
	show("big hash", ask("bigHash", [[]]), float([].hash()))
	show("decode", ask("decode", [PackedByteArray([255, 255, 255, 255])]), 4294967295.0)
	twin.free()
	node.queue_free()
	return true
