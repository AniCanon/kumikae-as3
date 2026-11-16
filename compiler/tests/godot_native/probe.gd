extends SceneTree

# Calls each method of Probe.as and prints what it gives: run_godot.py runs this with the interpreter and
# with the script as native code, and holds the two outputs to each other.
var killer: Node
var frames := 0


func _initialize() -> void:
	var probe := Node2D.new()
	probe.set_script(load("res://Probe.as"))
	root.add_child(probe)
	print("PROBE vectors=", probe.call("vectors"))
	print("PROBE values=", probe.call("values"))
	print("PROBE given=", probe.call("given"))
	print("PROBE carried=", probe.call("carried"))
	print("PROBE engine=", probe.call("engine"))
	print("PROBE script=", probe.call("script", 1000))
	print("PROBE script again=", probe.call("script", 10))
	print("PROBE listeners=", probe.call("listeners"))
	print("PROBE fail=", probe.call("fail", 3))
	print("PROBE after=", probe.call("script", 1))
	# A node whose script's object is freed while its _process runs: see Probe.as.
	killer = Node.new()
	killer.set_script(load("res://killer.gd"))
	root.add_child(killer)
	var victim := Node2D.new()
	victim.set_script(load("res://Probe.as"))
	root.add_child(victim)
	killer.set("victim", victim)
	victim.set("killer", killer)


func _process(_delta: float) -> bool:
	frames += 1
	if frames < 4:
		return false
	print("PROBE freed=", killer.get("said") if killer else "no killer")
	return true
