extends Node
## Exercises the script host: attach, engine callbacks, signals, an uncaught AS3 error, a missing method.

var node: Node
var frames := 0


func _ready() -> void:
	process_priority = 1000
	node = Node.new()
	node.set_script(ClassDB.class_call_static(&"SpikeScript", &"for_class", &"Probe"))
	add_child(node)


func _process(_delta: float) -> void:
	frames += 1
	if frames == 5:
		print("count=%d" % node.call(&"get_count"))
		node.call(&"ping")
		print("pinged=%d" % node.call(&"get_count"))
		node.call(&"explode")
	elif frames == 6:
		print("alive")
		# GDScript itself aborts a function that calls a missing method, so ask the instance directly.
		if not node.has_method(&"no_such_method") and node.has_method(&"get_count"):
			print("survived")
		get_tree().quit(0)
