extends SceneTree

func _initialize() -> void:
	var bench := Node2D.new()
	bench.set_script(load("res://Matrix.as"))
	var receiver := Node2D.new()
	var result = bench.call("methodKeysRead", receiver, 16)
	print("METHOD ", result == receiver.is_processing())
	receiver.free()
	bench.free()
	quit()
