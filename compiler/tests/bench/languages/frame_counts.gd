extends SceneTree

# Runs one of the frame benchmarks (frames/) for a few frames: run_godot.py holds what the native code
# leaves to the library meanwhile to frames/native_counts.txt. `-- <Class> <frames>`, with --fixed-fps so
# that each frame has one physics step.
var bench: Node2D
var left := 0


func _initialize() -> void:
	var asked := OS.get_cmdline_user_args()
	left = int(asked[1])
	bench = Node2D.new()
	bench.set_script(load("res://frames/%s.as" % asked[0]))
	root.add_child(bench)


func _process(_delta: float) -> bool:
	left -= 1
	if left > 0:
		return false
	print("COUNTED ", OS.get_cmdline_user_args()[0], " ", bench.call("checksum"))
	return true
