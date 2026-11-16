# A script stuck in a loop is stopped, as Flash stops one, with Error #1502, and the game goes on. run_godot.py
# runs this in a copy of the project whose settings give a limit of half a second.
extends SceneTree

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func _initialize() -> void:
	var node := Node.new()
	node.set_script(load("res://Spin.as"))
	root.add_child(node)
	var started := Time.get_ticks_msec()
	show("an endless loop is stopped", node.call("forever"), null)
	show("a catch does not stop it", node.call("caught"), null)
	show("nor a catch around the emit() that ran it", node.call("listened"), null)
	var took := Time.get_ticks_msec() - started
	# Three loops, each stopped at the limit of half a second. How soon after the limit a loop stops is held
	# with a clock the test steps (tests/test_vm.cpp): here the clock is the machine's, which may be busy.
	show("after the limit", took >= 1500, true)
	show("it ran until then", node.get("turns") > 1000, true)
	show("the next call runs", node.call("quick"), 7)
	node.free()
	quit()
