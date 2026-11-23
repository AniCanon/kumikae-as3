# Godot's values and objects held untyped in AS3, used by name. Each line shows what the AS3 script gave and
# what GDScript gives for the same thing with an untyped variable; run_godot.py checks the two agree.
extends SceneTree

var node: Node2D
var child: Node2D
var scripted: Node2D
var frames := 0

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func ask(method: String, arguments: Array = []):
	return Callable(node, method).callv(arguments)

func fresh() -> Node2D:
	if child:
		child.free()
	child = Node2D.new()
	child.name = "child"
	node.add_child(child)
	return child

func writes(o) -> Array:
	o.position = Vector2(3, 4)
	o.visible = false
	o["name"] = "renamed"
	var at = o.position
	at.x += 1
	o.position = at
	return [o.position, o.visible, str(o.name)]

func calls(o) -> Array:
	o.show()
	var inner = Node.new()
	o.add_child(inner)
	inner.name = "inner"
	return [o.visible, o.get_child_count(), str(o.get_child(0).name), o.is_ancestor_of(inner), o.get_node("inner") == inner]

func of_value() -> Array:
	var v = Vector2(3, 4)
	var color = Color(1, 0.5, 0)
	return [v.x, v.y, v.length(), v.normalized().y, color.g, color.lightened(0.5).r, v is Vector2, "a copy"]

func _initialize() -> void:
	node = Node2D.new()
	node.set_script(load("res://ByName.as"))
	node.name = "main"
	root.add_child(node)
	scripted = Node2D.new()
	scripted.set_script(load("res://ByName.as"))
	scripted.name = "other"
	root.add_child(scripted)

func _physics_process(_delta: float) -> bool:
	frames += 1
	if frames < 2:
		return false
	if frames > 2:
		return true
	var o = fresh()
	show("properties read by name", ask("reads", [o]), [str(o.name), str(o["name"]), o.visible, o.position, o.get_child_count(), o.get_parent() == node, true])
	show("properties written by name", ask("writes", [fresh()]), writes(fresh()))
	show("methods called by name", ask("calls", [fresh()]), calls(fresh()))
	show("a name that is not there", ask("missing", [fresh()]), [
			"Error #1069: Property nothing not found on Object and there is no default value.",
			"Error #1056: Cannot create property nothing on Object.", "Error #1006: nothing is not a function.", 1037, 1006, "refused", "refused"])
	o = fresh()
	show("in and hasOwnProperty", ask("has", [o]), ["name" in o, "nothing" in o, o.has_method("hide"), o.has_method("nothing")])
	show("a value's parts and methods", ask("ofValue"), of_value())
	show("a node that carries a script", ask("scripted", [scripted]), [9, 8, "other", false, true, true, true])
	show("the script's own object", ask("self"), [5, 6, "main", 0.5, true, node.get_child_count()])
	o = fresh()
	show("a method read by name", ask("method", [o]), [false, true, true, "function"])
	show("a signal reached by name", ask("listens", [fresh()]), [true, 1, 0])
	var gone := Node2D.new()
	var held = gone
	gone.free()
	show("a freed object", ask("freed", [held]), "TypeError 1009")
	child.free()
	scripted.free()
	node.free()
	return false
