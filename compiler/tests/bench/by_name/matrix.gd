extends SceneTree

func _initialize() -> void:
	measure.call_deferred()

func median(samples: Array) -> float:
	samples.sort()
	var mid := samples.size() >> 1
	return (float(samples[(samples.size() - 1) >> 1]) + float(samples[mid])) / 2.0

func batch(row: String, target: Variant, rounds: int, mode: String, targets: Array, script: Node, cs: Node) -> int:
	var kind := row.get_slice("/", 0)
	var op := row.get_slice("/", 1)
	var family: String = {"scriptEngineKeys": "engineKeys", "inherited": "node", "gdscript": "node", "foreign": "script", "custom": "custom" if op == "call" else "script"}.get(kind, kind)
	var method := StringName(family + op.capitalize())
	for child: Node2D in targets:
		child.position = Vector2(3, 4)
		child.rotation = 0
	script.set("score", 7)
	var start := Time.get_ticks_usec()
	var result
	if mode == "cs" and kind in ["engineKeys", "scriptEngineKeys"]:
		result = cs.call("EngineKeysRead", target, rounds)
	elif mode == "cs":
		result = cs.call("Measure", kind, op, target, rounds)
	elif mode == "self" and kind == "script":
		result = script.call("self" + op.capitalize(), rounds)
	else:
		result = script.call(method, target, rounds)
	var elapsed := Time.get_ticks_usec() - start
	var own := kind in ["script", "foreign", "custom"]
	var valid := true
	if kind in ["engineKeys", "scriptEngineKeys"]:
		valid = is_same(result, [target.position, target.modulate, target.rotation][(rounds - 1) % 3])
	elif kind == "eightFields":
		var tail := rounds % 8
		valid = result == (rounds / 8) * 112 + tail * (tail + 6)
	elif kind == "computed":
		valid = result == 8 * rounds - rounds % 2
	elif kind == "joinThree":
		valid = result == rounds * 18
	elif kind == "append":
		valid = result == rounds * 7
	elif kind == "appendLength":
		var tail := rounds % 512
		valid = result == 7 * ((rounds / 512) * 512 * 513 + tail * (tail + 1)) / 2
	elif kind == "joinLong":
		valid = result == target + target
	elif kind in ["customString", "customVector", "customFields"]:
		valid = result == rounds * {"customString": 12, "customVector": 10, "customFields": 15}[kind]
	elif op == "read" or (op == "call" and (own or kind == "value")):
		valid = result == rounds * (7 if own else (5 if op == "call" else (2.5 if kind.begins_with("configure") else 1 if kind.begins_with("timer") else 3)))
	elif kind in ["proven", "typed"] and op == "call":
		valid = absf(result - rounds * 0.0001) < rounds * 0.0000001
	elif kind in ["proven", "typed", "detached"]:
		valid = result == 17 if op == "write" else absf(angle_difference(result, rounds * 0.0001)) < rounds * 0.0000001
	elif op == "write" and own:
		valid = target.score == rounds - 1
	else:
		for i in targets.size() if kind == "mixed" else 1:
			var child: Node2D = targets[i] if kind == "mixed" else target
			var count: int = (rounds + 7 - i) / 8 if kind == "mixed" else rounds
			if op == "write":
				valid = valid and child.position == Vector2(8, 9)
			else:
				valid = valid and absf(angle_difference(child.rotation, count * 0.0001)) < maxf(0.002, count * 0.0000001)
	if not valid:
		printerr("Wrong result for ", row, " after ", rounds, " rounds: ", result)
		return -1
	return elapsed

func measure() -> void:
	var args := OS.get_cmdline_user_args()
	var managed := args[0] == "cs"
	var n := int(args[1])
	var script := Node2D.new()
	script.set_script(load("res://Matrix.as"))
	var foreign := Node2D.new()
	foreign.set_script(load("res://foreign.gd"))
	var custom := Node2D.new()
	custom.set_script(load("res://custom.gd"))
	root.add_child(custom)
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
	var cases := {"node": node, "inherited": script, "gdscript": foreign, "foreign": foreign, "computed": foreign, "eightFields": foreign, "custom": custom, "customString": custom, "customVector": custom, "customFields": custom, "script": script,
			"value": Vector2(3, 4), "alias": node, "aliasTyped": node, "proven": node, "typed": node, "detached": node, "mixed": node}
	var pieces := {"joinThree": "middle😀", "append": "abcdé😀", "appendLength": "abcdé😀", "joinLong": "ab".repeat(16384)}
	cases.merge(pieces)
	var engine_keys := {"engineKeys": node, "scriptEngineKeys": foreign}
	cases.merge(engine_keys)
	var inferred := {"configure": node, "timer": node, "number": node, "boolean": node, "string": node, "as3Class": node,
			"vector": Vector2(3, 0), "color": Color(0, 3, 0)}
	for kind: String in inferred:
		cases[kind] = inferred[kind]
		cases[kind + "Typed"] = inferred[kind]
	var pilot := args[2] == "pilot"
	var rows: Array[String] = []
	var sizes := {}
	var partners := {}
	for entry: String in args.slice(3):
		var row := entry.get_slice("=", 0)
		rows.append(row)
		if not pilot:
			sizes[row] = int(entry.get_slice("=", 1))
			if entry.get_slice_count("=") == 3:
				partners[row] = entry.get_slice("=", 2)
	if rows.is_empty():
		for kind: String in cases:
			for op in ["read", "write", "call"]:
				if (kind in ["computed", "eightFields"] or kind in engine_keys) and op != "read":
					continue
				if kind in pieces and (op != "read" or managed):
					continue
				if kind.trim_suffix("Typed") in inferred and (op != "read" or (managed and kind.trim_suffix("Typed") not in ["timer", "configure"])):
					continue
				if (kind == "foreign" and op == "write") or (kind == "custom" and op == "write") or (kind in ["customString", "customVector", "customFields"] and op != "read"):
					continue
				if kind == "detached" and op != "call":
					continue
				if kind == "value" and (op == "write" or managed):
					continue
				rows.append("%s/%s" % [kind, op])
	# Time equal windows in both directions so the same pair runs first and last equally often.
	var reverse := rows.duplicate()
	reverse.reverse()
	var windows := {}
	for row in rows:
		windows[row] = []
	var directions := [rows] if pilot else [rows, reverse, reverse, rows]
	for order: Array in directions:
		var visited := {}
		for first: String in order:
			if visited.has(first):
				continue
			var kind := first.get_slice("/", 0)
			var op := first.get_slice("/", 1)
			if (kind in ["foreign", "computed", "eightFields"] or kind.begins_with("custom")) and args[0] == "main":
				var target = cases[kind]
				var probe = script.call("probeComputed", target) if kind in ["computed", "eightFields"] else script.call("probeForeign", target, op == "read")
				if probe == (-1069 if op == "read" else -1006):
					continue
			var group := [first]
			if partners.has(first):
				group.append(partners[first])
				visited[partners[first]] = true
			var rounds := {}
			var batches := {}
			var totals := {}
			for row: String in group:
				rounds[row] = sizes.get(row, maxi(1, n / 100) if row.begins_with("configure") else n)
				batches[row] = []
				totals[row] = 0
			# Both members warm up together, then take turns in ABBA order until both windows are full.
			var warming := true
			while true:
				var ready := true
				for row: String in group:
					var elapsed := batch(row, cases[row.get_slice("/", 0)], rounds[row], args[0], targets, script, cs)
					if elapsed < 0:
						quit(1)
						return
					totals[row] += elapsed
					if warming:
						ready = ready and totals[row] >= (1000 if pilot else 20000)
					else:
						batches[row].append(elapsed)
						ready = ready and (batches[row].size() >= 3 if pilot else totals[row] >= 40000)
				group.reverse()
				if ready:
					if not warming:
						break
					warming = false
					for row: String in group:
						totals[row] = 0
			for row: String in group:
				var duration := median(batches[row])
				if not pilot and duration < 500:
					printerr("Batch below 0.5 ms for ", row, ": ", duration, " us")
					quit(1)
					return
				windows[row].append(duration * n / rounds[row])
	for row in rows:
		var times: Array = windows[row]
		print("NAMED ", row, " ", "unsupported" if times.is_empty() else str(median(times)))
	cs.free()
	quit()
