extends SceneTree

func _initialize() -> void:
	measure.call_deferred()

func measure() -> void:
	var args := OS.get_cmdline_user_args()
	var managed := args[0] == "cs"
	var n := int(args[1])
	var script := Node2D.new()
	script.set_script(load("res://Matrix.as"))
	var foreign := Node2D.new()
	foreign.set_script(load("res://foreign.gd"))
	var node := Node2D.new()
	var targets := [node, Sprite2D.new(), Marker2D.new(), Polygon2D.new(),
			Line2D.new(), Path2D.new(), foreign, script]
	var cs := Node.new()
	if managed:
		cs.set_script(load("res://Managed.cs"))
	for child: Node2D in targets:
		root.add_child(child)
		script.call("addTarget", child)
		if managed:
			cs.call("AddTarget", child)
	var cases := {"node": node, "inherited": script, "gdscript": foreign, "script": script,
			"value": Vector2(3, 4), "proven": node, "typed": node, "mixed": node}
	for kind: String in cases:
		var target = cases[kind]
		for op in ["read", "write", "call"]:
			if kind == "value" and (op == "write" or managed):
				continue
			var row := "%s/%s" % [kind, op]
			if args.size() > 2 and row not in args.slice(2):
				continue
			var family: String = {"inherited": "node", "gdscript": "node"}.get(kind, kind)
			var method := StringName(family + op.capitalize())
			for rounds: int in [512, n]:
				for child: Node2D in targets:
					child.position = Vector2(3, 4)
					child.rotation = 0
				script.set("score", 7)
				var start := Time.get_ticks_usec()
				var result
				if managed:
					result = cs.call("Measure", kind, op, target, rounds)
				else:
					result = script.call(method, target, rounds)
				var elapsed := Time.get_ticks_usec() - start
				var own := kind == "script"
				var valid := true
				if op == "read" or (op == "call" and (own or kind == "value")):
					valid = result == rounds * (7 if own else (5 if op == "call" else 3))
				elif op == "write" and own:
					valid = target.score == rounds - 1
				else:
					for i in targets.size() if kind == "mixed" else 1:
						var child: Node2D = targets[i] if kind == "mixed" else target
						var count: int = (rounds + 7 - i) / 8 if kind == "mixed" else rounds
						if op == "write":
							valid = valid and child.position == Vector2(8, 9)
						else:
							valid = valid and absf(child.rotation - count * 0.0001) < 0.002
				if not valid:
					printerr("Wrong result for ", row, " after ", rounds, " rounds: ", result)
					quit(1)
					return
				if rounds == n:
					print("NAMED ", row, " ", elapsed)
	cs.free()
	quit()
