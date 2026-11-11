extends Node
## Exercises the version 2 host: error traces built on the error path, native closures, stale closures.

var node: Node
var frames := 0


func _ready() -> void:
	process_priority = 1000
	node = Node.new()
	node.set_script(ClassDB.class_call_static(&"SpikeScript", &"for_class", &"V2Probe"))
	add_child(node)


func _process(_delta: float) -> void:
	frames += 1
	if frames != 3:
		return
	node.call(&"fire")
	print("after_fire=%d" % node.call(&"get_state"))
	node.call(&"chain")
	print("alive")
	var closure: Callable = node.call(&"get_closure")
	print("closure_valid=%s" % closure.is_valid())
	closure.call()
	print("called=%d" % node.call(&"get_state"))
	node.free()
	print("valid_after_free=%s" % closure.is_valid())
	get_tree().quit(0)
