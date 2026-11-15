# An AS3 script's getters and setters, reached from GDScript as properties. Each line shows what the AS3
# script gave and what GDScript expects; run_godot.py checks the two agree.
extends SceneTree

var node: Node2D
var frames := 0

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func _initialize() -> void:
	node = Node2D.new()
	node.set_script(load("res://Accessors.as"))
	root.add_child(node)

func _physics_process(_delta: float) -> bool:
	frames += 1
	if frames < 2:
		return false
	if frames > 2:
		return true
	show("getter", node.health, 10)
	node.health = 25
	show("setter", node.health, 25)
	show("setter ran once", node.sets, 1)
	show("get by name", node.get("health"), 25)
	node.set("health", 3)
	show("set by name", node.health, 3)
	show("getter alone", node.label, "hp 3")
	node.set("label", "changed")
	show("no setter, no change", node.label, "hp 3")
	node.only = 4.0
	show("setter alone", node.health, 8)
	show("setter alone reads as nothing", node.get("only"), null)
	show("a Godot value", node.spot, Vector2(8, 1))
	node.set("health", "text")
	show("a value of the wrong kind is refused", node.health, 8)
	node.health += 1
	show("compound", node.health, 9)
	show("through an interface", node.ownSize(), 90)
	show("a getter's error reads as nothing", node.broken, null)
	show("a native property still", node.position, Vector2.ZERO)
	node.position = Vector2(3, 4)
	show("a native property set", node.position, Vector2(3, 4))
	node.queue_free()
	return false
