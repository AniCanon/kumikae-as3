extends SceneTree

func _initialize() -> void:
	measure.call_deferred()

func median(samples: Array) -> float:
	samples.sort()
	var mid := samples.size() >> 1
	return (float(samples[(samples.size() - 1) >> 1]) + float(samples[mid])) / 2.0

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
	var cases := {"node": node, "inherited": script, "gdscript": foreign, "foreign": foreign, "script": script,
			"value": Vector2(3, 4), "alias": node, "aliasTyped": node, "proven": node, "typed": node, "mixed": node}
	var inferred := {"configure": node, "timer": node, "number": node, "boolean": node, "string": node, "as3Class": node,
			"vector": Vector2(3, 0), "color": Color(0, 3, 0)}
	for kind: String in inferred:
		cases[kind] = inferred[kind]
		cases[kind + "Typed"] = inferred[kind]
	var rows := args.slice(2)
	if rows.is_empty():
		for kind: String in cases:
			for op in ["read", "write", "call"]:
				if kind.trim_suffix("Typed") in inferred and (op != "read" or (managed and kind.trim_suffix("Typed") not in ["timer", "configure"])):
					continue
				if kind == "foreign" and op == "write":
					continue
				if kind == "value" and (op == "write" or managed):
					continue
				rows.append("%s/%s" % [kind, op])
	# Time equal windows in both directions so the same pair runs first and last equally often.
	var reverse := rows.duplicate()
	reverse.reverse()
	var order := rows + reverse + reverse + rows
	var windows := {}
	for row in order:
		var kind := row.get_slice("/", 0)
		var op := row.get_slice("/", 1)
		var target = cases[kind]
		if kind == "foreign" and args[0] == "main":
			var probe = script.call("probeForeign", target, op == "read")
			if probe == (-1069 if op == "read" else -1006):
				windows[row] = []
				continue
		var family: String = {"inherited": "node", "gdscript": "node", "foreign": "script"}.get(kind, kind)
		var method := StringName(family + op.capitalize())
		# Accumulate measured batches so short native rows outlast the clock resolution
		# and both controls reach steady state before their recorded work begins.
		var rounds := n * 10 if kind.trim_suffix("Typed") in inferred else n
		if kind.begins_with("configure"):
			rounds = maxi(1, n / 100)
		var warm_us := 0
		var timed_us := 0
		var batches := []
		while timed_us < 40000:
			for child: Node2D in targets:
				child.position = Vector2(3, 4)
				child.rotation = 0
			script.set("score", 7)
			var start := Time.get_ticks_usec()
			var result
			if managed:
				result = cs.call("Measure", kind, op, target, rounds)
			elif args[0] == "self" and kind == "script":
				result = script.call("self" + op.capitalize(), rounds)
			else:
				result = script.call(method, target, rounds)
			var elapsed := Time.get_ticks_usec() - start
			var own := kind in ["script", "foreign"]
			var valid := true
			if op == "read" or (op == "call" and (own or kind == "value")):
				valid = result == rounds * (7 if own else (5 if op == "call" else (2.5 if kind.begins_with("configure") else 1 if kind.begins_with("timer") else 3)))
			elif kind in ["proven", "typed"]:
				valid = result == 17 if op == "write" else absf(result - rounds * 0.0001) < 0.002
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
			if warm_us < 20000:
				warm_us += elapsed
			else:
				timed_us += elapsed
				batches.append(elapsed)
		if not windows.has(row):
			windows[row] = []
		windows[row].append(median(batches) * n / rounds)
	for row in rows:
		var times: Array = windows[row]
		print("NAMED ", row, " ", "unsupported" if times.is_empty() else str(median(times)))
	cs.free()
	quit()
