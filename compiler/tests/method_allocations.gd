extends SceneTree

class WithMethod extends Node2D:
	func probe_method() -> bool:
		return true

class ReplaceScript extends RefCounted:
	class Override extends Node2D:
		@warning_ignore("native_method_override")
		func get_child_count(_internal := false) -> int:
			return 27

	class Getter extends Node2D:
		func _get(key: StringName):
			return 19 if key == &"get_child_count" else null

	func replace(target: Node2D, getter: bool) -> void:
		target.set_block_signals(true)
		target.set_script(Getter if getter else Override)
		target.set_block_signals(false)

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
	var changer := ReplaceScript.new()
	print("CHANGED READ ", bench.call("changedRead", bare, "getChildCount", changer) == 19)
	bare.set_script(null)
	print("CHANGED CALL ", bench.call("changedCall", bare, changer) == 27)
	bare.free()
	receiver.free()
	bench.free()
	quit()
