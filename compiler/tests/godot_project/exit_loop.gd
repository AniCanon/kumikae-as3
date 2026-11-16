# A main loop that is not a scene tree: an object only a static field holds, with a GDScript listener on
# it, is still held when Godot shuts down. Nothing may crash then. run_godot.py checks how Godot exits.
extends MainLoop

var frames := 0

func _process(_delta: float) -> bool:
	frames += 1
	if frames < 2:
		return false
	var node := Node.new()
	node.set_script(load("res://Signals.as"))
	var at_exit := Resource.new()
	at_exit.changed.connect(func(): pass)
	node.keepPlain(at_exit)
	node.free()
	print("EXIT LOOP ready")
	return true
