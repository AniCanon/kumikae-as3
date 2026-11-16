extends SceneTree

# Calls each method of Probe.as and prints what it gives: run_godot.py runs this with the interpreter and
# with the script as native code, and holds the two outputs to each other.
func _initialize() -> void:
	var probe := Node2D.new()
	probe.set_script(load("res://Probe.as"))
	root.add_child(probe)
	print("PROBE vectors=", probe.call("vectors"))
	print("PROBE engine=", probe.call("engine"))
	print("PROBE script=", probe.call("script", 1000))
	print("PROBE script again=", probe.call("script", 10))
	print("PROBE listeners=", probe.call("listeners"))
	print("PROBE speaks=", probe.call("speaks"))
	print("PROBE fail=", probe.call("fail", 3))
	print("PROBE after=", probe.call("script", 1))
	quit()
