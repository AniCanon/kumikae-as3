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
	receiver.free()
	bench.free()
	quit()
