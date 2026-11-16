# The names Godot asks a script about: what is kept of them is bounded by the script's members, whatever
# names a game asks for. run_godot.py also runs this with --verbose: no name is left behind at exit.
extends SceneTree

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func grown(o: Object, tag: String) -> int:
	var before := OS.get_static_memory_usage()
	for i in 200000:
		o.get("%s_unreached_%d" % [tag, i])
	return OS.get_static_memory_usage() - before

func _initialize() -> void:
	var scripted := RefCounted.new()
	scripted.set_script(load("res://Counted.as"))
	scripted.set("n", 6)
	var plain := RefCounted.new()
	var by_plain := grown(plain, "plain")
	var by_scripted := grown(scripted, "scripted")
	print("NAMES grown plain=%d scripted=%d" % [by_plain, by_scripted])
	show("names that reach nothing are not kept", by_scripted - by_plain < 1000000, true)
	show("a field is still reached", [scripted.get("n"), scripted.call("hello")], [6, "hello"])
	quit()
