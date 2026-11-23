# Godot's classes as class values in AS3. Each line shows what the AS3 script gave and what GDScript gives for
# the same thing; run_godot.py checks the two agree.
extends SceneTree

var node: Node2D
var frames := 0

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func ask(method: String, arguments: Array = []):
	return Callable(node, method).callv(arguments)

func made() -> Array:
	var kind := Sprite2D
	var made: Variant = kind.new()
	var typed: Node2D = kind.new()
	var said := [is_instance_of(made, kind), made is Node2D, made is Node, made is Timer, (made as Sprite2D) == made, (made as Timer) == null,
			typed is Sprite2D, is_instance_of(null, kind), is_instance_of(5, kind), is_instance_of(made, kind)]
	made.free()
	typed.free()
	return said

func _initialize() -> void:
	node = Node2D.new()
	node.set_script(load("res://Kinds.as"))
	root.add_child(node)

func _physics_process(_delta: float) -> bool:
	frames += 1
	if frames < 2:
		return false
	if frames > 2:
		return true
	show("new, is and as with a class value", ask("made"), made())
	show("a script object is its Godot object", ask("own"), [node is Node2D, node is CanvasItem, is_instance_of(node, Control), node is Object])
	show("a value type as a class value", ask("values"),
			[Vector2(1.5, 2), Vector2(), Color(1, 0.5, 0), true, false, true, true, true])
	var counted := RefCounted.new()
	show("a reference-counted class", ask("counted"), [counted is RefCounted, counted is Resource, counted.get_reference_count() > 0])
	show("a cast of a node", ask("cast", [node]) == node, true)
	show("a cast of null", ask("cast", [null]), null)
	show("a cast of a number fails", ask("cast", [5]), null)
	show("a cast of a value fails as it does where the class is named", ask("castFails"), ask("castNamedFails"))
	show("the same class", ask("same"), [true, false, true, "node", "vector", "[class Node2D]", "[class GodotObject]", true, "object"])
	show("a class the engine won't create", ask("refused"), ["refused", "refused"])
	node.free()
	return false
