extends SceneTree

class WithMethod extends Node2D:
	func probe_method() -> bool:
		return true

func _initialize() -> void:
	var bench := Node2D.new()
	bench.set_script(load("res://Matrix.as"))
	var receiver := Node2D.new()
	var result = bench.call("methodKeysRead", receiver, 16)
	print("METHOD ", result == receiver.is_processing())
	receiver.set_script(WithMethod)
	print("SCRIPT METHOD ", bench.call("readMethod", receiver, "probe_method", 16))
	var bare := Node2D.new()
	for i in 7:
		bench.call("addTarget", receiver)
	receiver.set_process(true)
	bare.set_process(false)
	print("MIXED ", not bench.call("mixedMethodsRead", bare, 16))
	bare.free()
	receiver.free()
	bench.free()
	quit()
