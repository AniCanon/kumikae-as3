# Vectors whose places hold Godot's values themselves (Places.as). Each line shows what the AS3 script gave
# and what GDScript gives for the same thing; run_godot.py checks the two agree.
extends SceneTree

var node: Node2D
var frames := 0

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func ask(method: String, arguments: Array = []):
	return Callable(node, method).callv(arguments)

func joined(values: Array, between: String) -> String:
	var texts := PackedStringArray()
	for value in values:
		texts.append(str(value))
	return between.join(texts)

func _initialize() -> void:
	node = Node2D.new()
	node.set_script(load("res://Places.as"))
	node.position = Vector2(100, 200)
	root.add_child(node)

func grown() -> String:
	var v := PackedVector2Array()
	v.resize(3)
	v.append(Vector2(1, 2))
	var count := v.size()
	v.append(Vector2(3, 4))
	var first := v[0]
	first.x = 9
	v[1] = first
	v[2] = v[3] * 2 + v[4]
	for i in 40:
		v.append(Vector2(i, -i))
	return "%d %d %s %s %s %s %s %s %s" % [count, v.size(), joined(Array(v.slice(0, 6)), " "), v[44], int(v[0].x), v[0] == v[1], v[1] == first, false, true]

func refused() -> String:
	var range := "Error #1125: The index %d is out of range 2."
	return joined(["Error #1034: Type Coercion failed: cannot convert null to Vector2.", 2, 2, 2, 2, 2, 2, "literal", "made", range % 2, range % 3,
			"Error #1126: Cannot change the length of a fixed Vector.", Vector2(3, 4), joined([Vector2(1, 2), Vector2(3, 4)], ",")], "|")

func methods() -> String:
	var said := []
	var v: Array[Vector2] = [Vector2(3, 0), Vector2(1, 1), Vector2(2, 2)]
	said.append_array([v.pop_back(), v.pop_front(), v.size()])
	v.push_front(Vector2(8, 8))
	v.push_front(Vector2(7, 7))
	said.append(v.size())
	v.insert(1, Vector2(5, 5))
	said.append(v.pop_front())
	said.append(joined(v, ","))
	var taken: Array[Vector2] = [v[1]]
	v = [v[0], Vector2(4, 4), Vector2(6, 6), Vector2(0, 9), v[2]]
	said.append_array([joined(taken, ","), taken.size(), joined(v, ",")])
	said.append(joined(v.slice(1, 3), ","))
	said.append(joined(v + taken + [Vector2(1, 1)], ","))
	v.reverse()
	said.append(joined(v, ","))
	said.append_array([v.find(Vector2(4, 4)), v.rfind(Vector2(4, 4)), v.find(Vector2(4, 5)), v.find(v[3], 1)])
	v.sort_custom(func(a, b): return a.x < b.x)
	said.append(joined(v, ","))
	var total := Vector2()
	var keys := ""
	for i in v.size():
		total += v[i]
		keys += str(i)
	said.append_array([total, keys])
	said.append(joined(v.map(func(item): return item * 2), ","))
	said.append(joined(v.filter(func(item): return item.x > 4), ","))
	said.append_array([v.any(func(item): return item.y == 9), v.all(func(item): return item.y == 9)])
	said.append_array([true, true, 0, true, true])
	said.append_array([joined([Vector2(2, 1), v[0]], ","), 2])
	return joined(said, "|")

func _physics_process(_delta: float) -> bool:
	frames += 1
	if frames < 2:
		return false
	if frames > 2:
		return true
	show("grown", ask("grown"), grown())
	show("no value is refused", ask("refused"), refused())
	show("methods", ask("methods"), methods())
	var line := Line2D.new()
	var keeper := Node.new()
	var points := PackedVector2Array([Vector2(), Vector2(0, 0), Vector2(2, 1), Vector2(3, 1.5), Vector2(4, 2)])
	show("given: counts", ask("given", [line, keeper]), 0 * 100 + 5 * 10 + Geometry2D.convex_hull(points).size())
	show("given: a packed array", [line.points, typeof(line.points)], [points, TYPE_PACKED_VECTOR2_ARRAY])
	show("given: an Array of values", [keeper.get_meta("points"), typeof(keeper.get_meta("points")[1])], [Array(points), TYPE_VECTOR2])
	show("given: inside an Array", keeper.get_meta("nested"), [[Vector2(), Vector2(0, 0)], [Vector2i(1, -2)]])
	show("given: none", keeper.get_meta("none"), [])
	show("given: whole numbers", [keeper.get_meta("cells"), typeof(keeper.get_meta("cells")[0])],
			[[Vector2i(6, -8), Vector2i(3, -4), Vector2i(2147483647, -2147483648)], TYPE_VECTOR2I])
	var shade := Gradient.new()
	var red := Color(1, 0, 0.5)
	var pale := Color(0.25, 0, 0)
	show("a larger value type", ask("wider", [shade]), joined([2, 4, Color(), Color(0, 1, 0), true, red, pale, red, 2, 3, true, 3, 22,
			Vector3(20, 2, -16), Vector3(1, 2, 3), false, joined([Vector3(18, 0, -18), Vector3(19, 0, -19)], ",")], "|"))
	show("a larger value type: to Godot", shade.colors, PackedColorArray([Color(), Color(0, 1, 0), pale]))
	line.position = Vector2(-1, -2)
	var list := [Vector2(0.5, 0.25), Vector3(1, 2, 3), null]
	var cannot := "Error #1034: Type Coercion failed: cannot convert %s to Vector2."
	show("taken", ask("taken", [line, list]), joined([
			joined([Vector2(0, 0), list[0], line.position, Vector2(100, 200), list[0]], ","), cannot % "Object", 5,
			joined([list[0], Vector2(0, 0)], ","), cannot % "Object", list[0], 6, 0, cannot % "Object", 6, list[0],
			"Error #1034: Type Coercion failed: cannot convert Object to Vector2i.", Vector2i(1, 2)], "|"))
	line.free()
	keeper.free()
	return true
