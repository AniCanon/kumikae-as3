extends SceneTree

func _initialize() -> void:
	measure.call_deferred(self)

static func measure(tree: SceneTree) -> void:
	var script := Node2D.new()
	script.set_script(load("res://Untyped.as"))
	tree.root.add_child(script)
	var node := Node2D.new()
	tree.root.add_child(node)
	var cs := Node2D.new()
	var args := OS.get_cmdline_user_args()
	var cases := ["script", "inherited", "node", "value", "refused"]
	if "script-only" in args:
		cases = ["script"]
	if "cs" in args:
		cs.set_script(load("res://CmpCs.cs"))
		tree.root.add_child(cs)
		cases = ["script", "inherited", "node"]
	for which in cases:
		for rounds in [1000, 20000]:
			var begin := Time.get_ticks_usec()
			var result
			if "cs" in args:
				result = cs.call("NamedLoop", node if which == "node" else script, which == "script", rounds)
			elif which == "node" or which == "inherited":
				result = script.call("nodeLoop", node if which == "node" else script, rounds)
			else:
				result = script.call(which + "Loop", rounds)
			var elapsed := Time.get_ticks_usec() - begin
			var expected: int = rounds * 8 if which == "value" else rounds
			if result != expected:
				printerr("%s returned %s, expected %d" % [which, str(result), expected])
				tree.quit(1)
				return
			if rounds == 20000:
				print("NAMED %s %d" % [which, elapsed])
	cs.free()
	tree.quit()
