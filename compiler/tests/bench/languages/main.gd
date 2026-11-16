extends Node
var nodes := {}
func _ready() -> void:
	var cs := Node2D.new()
	cs.set_script(load("res://CmpCs.cs"))
	add_child(cs)
	nodes["cs"] = cs
	var gd := Node2D.new()
	gd.set_script(load("res://CmpGd.gd"))
	add_child(gd)
	nodes["gd"] = gd
	var a := Node2D.new()
	a.set_script(load("res://CmpAs.as"))
	add_child(a)
	nodes["as"] = a
	await get_tree().process_frame
	var names := {"cs": ["Spin", "Work", "Vec", "Vec3"], "gd": ["spin", "work", "vec", "vec3"], "as": ["spin", "work", "vec", "vec3"]}
	var sizes := [200000, 3000000, 1000000, 1000000]
	for t in sizes.size():
		for who in ["gd", "cs", "as"]:
			var best := 1 << 62
			for r in 40:
				var start := Time.get_ticks_usec()
				nodes[who].call(names[who][t], sizes[t] if r >= 35 else sizes[t] / 100)
				if r >= 35:
					best = min(best, Time.get_ticks_usec() - start)
			print("CMP %s %s %d" % [names["gd"][t], who, best])
	get_tree().quit()
