# A reload asked for in the middle of AS3 in a game that runs as native code (AS3 emits a signal, GDScript
# hears it and asks, with a script edited since the game started): refused, and the game runs on as it
# was. run_godot.py runs this in a copy of the project whose library Play built in a run before.
extends SceneTree

func _initialize() -> void:
	var emitter := Node.new()
	emitter.set_script(load("res://Signals.as"))
	root.add_child(emitter)
	var file := FileAccess.open("res://Plain.as", FileAccess.READ_WRITE)
	file.seek_end()
	file.store_string("\n// Edited while the game runs.\n")
	file.close()
	emitter.renamed.connect(func():
		for i in Engine.get_script_language_count():
			if Engine.get_script_language(i).has_method("reload_scripts"):
				Engine.get_script_language(i).call("reload_scripts"))
	emitter.call("fire")
	print("GUARD fired")
	emitter.free()
	quit()
