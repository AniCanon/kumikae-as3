# Godot's methods as values in AS3. Each line shows what the AS3 script gave and what GDScript gives for the
# same thing; run_godot.py checks the two agree.
extends SceneTree

signal pinged

var node: Node2D
var child: Node2D
var other: Node2D
var frames := 0

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func ask(method: String, arguments: Array = []):
	return Callable(node, method).callv(arguments)

func _initialize() -> void:
	node = Node2D.new()
	node.set_script(load("res://Callables.as"))
	root.add_child(node)
	child = Node2D.new()
	other = Node2D.new()
	node.add_child(child)
	node.add_child(other)
	ask("setUp", [child, other])

func listens() -> Array:
	pinged.connect(child.hide)
	pinged.connect(node.hide)
	pinged.emit()
	var got := [child.visible, other.visible, node.visible, pinged.get_connections().size()]
	pinged.disconnect(child.hide)
	pinged.disconnect(node.hide)
	return got

func held() -> Array:
	child.show()
	var f := child.hide
	f.call()
	var before := child.visible
	pinged.connect(f)
	var has := pinged.is_connected(child.hide)
	pinged.disconnect(child.hide)
	return [before, has, pinged.is_connected(f), pinged.get_connections().size()]

func counted() -> Array:
	child.remove_from_group("tagged")
	child.remove_from_group("kept")
	var f := child.hide
	var add := child.add_to_group
	add.call("tagged")
	add.call("kept", false)
	return ["Error #1063: Argument count mismatch on godot.CanvasItem/hide(). Expected 0, got 1.",
			"Error #1063: Argument count mismatch on godot.Node/addToGroup(). Expected 1, got 0.", 1034,
			child.is_in_group("tagged"), child.is_in_group("kept"), f.get_argument_count(), add.get_argument_count()]

func _physics_process(_delta: float) -> bool:
	frames += 1
	if frames < 2:
		return false
	if frames > 2:
		return true
	show("a listener that is one of Godot's methods", ask("listens"), listens())
	show("held, called and then given to Godot", ask("held"), held())
	show("passed where a Function is declared", ask("passed"), [true, false, 0])
	show("the same method of the same object", ask("same"),
			[child.hide == child.hide, true, true, child.hide == other.hide, child.hide == child.show, node.hide == node.hide, true, "function"])
	ask("connectHide")
	var connection: Dictionary = node.get_signal_connection_list("pinged")[0]
	show("Godot's own Callable is what the signal holds", [connection.callable == child.hide, connection.callable.is_custom()], [true, false])
	child.remove_from_group("tagged")
	child.remove_from_group("kept")
	show("arguments are counted and converted", ask("counted"), counted())
	show("call and apply", ask("applied"), [child.is_in_group.callv(["tagged"]), child.is_in_group.call("missing")])
	show("a typed callback", ask("typed"), [false, child.is_in_group("tagged")])
	show("a method Godot's collection calls", ask("filtered"), node.get_children().filter(node.is_ancestor_of).size())
	show("statics, a global function and a singleton", ask("statics"),
			[Vector2.from_angle(0), lerpf(2, 6, 0.3), Input.is_action_pressed("ui_accept"), true, true])
	show("a singleton's method as a listener", ask("staticListener"), true)
	show("a method of a value", ask("ofValue"), [Vector2(1, 2).length(), Vector2(1, 2).dot(Vector2(0.5, 0.5)), true])
	var gone := Node2D.new()
	ask("keep", [gone])
	gone.free()
	show("called after its object is freed", ask("afterFree"), "TypeError 1009")
	show("added after its object is freed", ask("listenFreed"), "TypeError 1009")
	ask("drop")
	node.free()
	return false
