# The faults the review of the first Godot slice found. Each line of output is checked by run_godot.py.
extends SceneTree

var node: Node
var derived: CharacterBody2D
var frames := 0

func attach(path: String, to: Object) -> void:
	# The script is held by nothing but the object it is attached to.
	to.set_script(load(path))

func _initialize() -> void:
	node = Node.new()
	attach("res://Checks.as", node)
	var child := Node.new()
	node.add_child(child)
	root.add_child(node)
	derived = CharacterBody2D.new()
	attach("res://Derived.as", derived)
	root.add_child(derived)

func _physics_process(_delta: float) -> bool:
	frames += 1
	if frames < 4:
		return false
	if frames > 4:
		return true # A check above failed part of the way: stop, do not repeat.
	# 1. The script outlives the variable that loaded it.
	print("CHECK script alive=%s" % (node.get_script() != null and node.get_script().can_instantiate()))
	# 3. An override of an override is the one the engine calls: 3 frames at 100 each.
	print("CHECK derived steps=%d" % derived.steps)
	# 5. super._ready() runs Godot's own, which does nothing, once.
	print("CHECK ready=%d" % node.readied)
	# 2. A call on an object Godot has freed is an AS3 error, not a crash.
	node.keep()
	node.get_child(0).free()
	node.countOfKept()
	print("CHECK survived freed object")
	# 7. Arguments are checked on the way in. (callv: a refused call gives null and this function carries on.)
	print("CHECK two=%s" % str(Callable(node, "two").callv([1, 2])))
	print("CHECK too few=%s" % str(Callable(node, "two").callv([1])))
	print("CHECK default=%s" % str(Callable(node, "withDefault").callv([1])))
	print("CHECK int=%s" % str(Callable(node, "takeInt").callv([42])))
	print("CHECK big int=%s" % str(Callable(node, "takeInt").callv([5000000000])))
	print("CHECK negative uint=%s" % str(Callable(node, "takeUint").callv([-1])))
	print("CHECK number from text=%s" % str(Callable(node, "takeNumber").callv(["abc"])))
	print("CHECK node from int=%s" % str(Callable(node, "takeNode").callv([5])))
	print("CHECK string from null=%s" % str(Callable(node, "takeString").callv([null])))
	print("CHECK flag=%s" % str(Callable(node, "takeFlag").callv([true])))
	# 6. A script on a reference-counted object does not keep it alive.
	var counted := RefCounted.new()
	attach("res://Counted.as", counted)
	print("CHECK counted references=%d hello=%s" % [counted.get_reference_count(), counted.hello()])
	var weak: WeakRef = weakref(counted)
	counted = null
	print("CHECK counted freed=%s" % (weak.get_ref() == null))
	# 8. Two files of one name in different folders are different classes.
	var first := Node.new()
	attach("res://a/Thing.as", first)
	var second := Node.new()
	attach("res://b/Thing.as", second)
	print("CHECK things=%s %s" % [first.which(), second.which()])
	first.free()
	second.free()
	# A Godot value or object must be of the type the parameter names.
	print("CHECK vector=%s" % str(Callable(node, "takeVector").callv([Vector2(3, 4)])))
	print("CHECK wrong vector=%s" % str(Callable(node, "takeVector").callv([Vector3(1, 2, 3)])))
	print("CHECK body=%s" % str(Callable(node, "takeBody").callv([derived])))
	print("CHECK no body=%s" % str(Callable(node, "takeBody").callv([null])))
	print("CHECK wrong body=%s" % str(Callable(node, "takeBody").callv([node])))
	# An object that is not there is null, however GDScript came by it.
	print("CHECK missing node=%s" % str(Callable(node, "takeNode").callv([node.get_node_or_null("missing")])))
	print("CHECK no instance=%s" % str(Callable(node, "takeNode").callv([instance_from_id(0)])))
	print("CHECK null script object=%s" % str(Callable(node, "nobody").callv([])))
	# A script is only attached to an object of the Godot class it extends.
	var wrong := Node.new()
	attach("res://Base.as", wrong)
	print("CHECK wrong node has script=%s" % (wrong.get_script() != null and wrong.has_method("_physics_process")))
	wrong.free()
	# A class that extends no Godot class is not a script.
	var plain := load("res://Plain.as") as Script
	print("CHECK plain can attach=%s" % (plain != null and plain.can_instantiate()))
	# A script object can't be handed to Godot yet: null, and it says so.
	print("CHECK returned script object=%s" % str(Callable(node, "myself").callv([])))
	node.queue_free()
	derived.queue_free()
	return true
