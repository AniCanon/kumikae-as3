# A script object of a reference-counted class, made with `new` in AS3, lives as long as AS3 or Godot holds
# it, and no longer: neither keeps the other alive in a loop. Each line shows what happened and what should.
extends SceneTree

var node: Node

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func _initialize() -> void:
	node = Node.new()
	node.set_script(load("res://Kept.as"))
	root.add_child(node)

	show("made", node.call("make"), "hello 7 kept")
	var counted = node.get("counted")
	var stored = node.get("resource")
	show("alive while AS3 holds them", [counted is RefCounted, counted.hello(), counted.get("n"), stored is Resource, stored.get("label")],
			[true, "hello", 7, true, "kept"])
	var weak_counted: WeakRef = weakref(counted)
	var weak_stored: WeakRef = weakref(stored)
	counted = null
	stored = null
	show("still alive when Godot lets go", [weak_counted.get_ref() != null, weak_stored.get_ref() != null], [true, true])
	show("and AS3 still sees them", node.call("make") != "", true)

	# Held by both, let go by AS3 first: Godot's hold keeps it, with its fields.
	counted = node.get("counted")
	weak_counted = weakref(counted)
	node.call("drop")
	show("alive while Godot holds it", [weak_counted.get_ref() != null, counted.get("n"), counted.hello()], [true, 7, "hello"])
	counted = null
	show("freed when both let go", weak_counted.get_ref() == null, true)

	node.call("make")
	weak_counted = weakref(node.get("counted"))
	weak_stored = weakref(node.get("resource"))
	node.call("drop")
	show("freed when AS3 lets go", [weak_counted.get_ref() == null, weak_stored.get_ref() == null], [true, true])

	var given = node.call("giveCounted")
	var given_stored = node.call("giveStored")
	show("given to Godot, alive while Godot holds them", [given.get("n"), given.hello(), given_stored.get("label")], [11, "hello", "given"])
	weak_counted = weakref(given)
	weak_stored = weakref(given_stored)
	given = null
	given_stored = null
	show("freed when Godot lets go", [weak_counted.get_ref() == null, weak_stored.get_ref() == null], [true, true])

	var lost: int = node.call("makeAndForget")
	show("made and forgotten in AS3", [lost != 0, is_instance_id_valid(lost)], [true, false])
	var listening_to_itself: int = node.call("selfListening")
	show("a resource that listens to itself goes", [listening_to_itself != 0, is_instance_id_valid(listening_to_itself)], [true, false])
	var other := Node.new()
	var listener: int = node.call("listening", other)
	show("a listener on another node goes", [listener != 0, is_instance_id_valid(listener)], [true, false])
	other.name = "renamed"
	other.free()

	# Attached by GDScript, held by AS3 too once its own method kept it: AS3's hold keeps it.
	var attached := RefCounted.new()
	attached.set_script(load("res://Counted.as"))
	attached.set("n", 3)
	attached.call("hold")
	var weak_attached: WeakRef = weakref(attached)
	attached = null
	show("attached, kept while AS3 holds it", [weak_attached.get_ref() != null, node.call("heldN")], [true, 3])
	node.call("letGoOfHeld")
	show("attached, freed when AS3 lets go too", weak_attached.get_ref() == null, true)
	node.free()
	quit()
