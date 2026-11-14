# A benchmark, not a test: how long calls from AS3 into the engine take. See tests/bench/run_bench.py.
extends SceneTree

var node: Node2D
var frames := 0

func _initialize() -> void:
	node = Node2D.new()
	node.set_script(load("res://Bench.as"))
	root.add_child(node)

func _physics_process(_delta: float) -> bool:
	frames += 1
	if frames < 2:
		return false
	node.call("spin", 1000)
	var start := Time.get_ticks_usec()
	var result = node.call("spin", 40000)
	print("BENCH engine_calls %d %s" % [Time.get_ticks_usec() - start, str(result)])
	node.free()
	return true
