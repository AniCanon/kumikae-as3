# AS3's vectors handed to Godot. Each line shows what the AS3 script gave and what GDScript gives for the
# same thing; run_godot.py checks the two agree.
extends SceneTree

var node: Node2D
var frames := 0

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func ask(method: String, arguments: Array = []):
	return Callable(node, method).callv(arguments)

func _initialize() -> void:
	node = Node2D.new()
	node.set_script(load("res://Vectors.as"))
	root.add_child(node)

func _physics_process(_delta: float) -> bool:
	frames += 1
	if frames < 2:
		return false
	if frames > 2:
		return true
	var line := Line2D.new()
	ask("line", [line])
	show("points", line.points, PackedVector2Array([Vector2(1, 2), Vector2(3, 4), Vector2(5, 6)]))
	show("points type", typeof(line.points), TYPE_PACKED_VECTOR2_ARRAY)
	line.free()
	show("hull", ask("hull"), Geometry2D.convex_hull(PackedVector2Array([Vector2(0, 0), Vector2(4, 0), Vector2(2, 1), Vector2(4, 4), Vector2(0, 4)])))
	show("bytes", ask("bytes"), Marshalls.raw_to_base64(PackedByteArray([72, 105, 255])))
	show("empty", ask("empty"), Marshalls.raw_to_base64(PackedByteArray()))
	show("too big for a byte", ask("tooBig"), null)
	show("below a byte", ask("negative"), null)
	var gradient := Gradient.new()
	ask("shade", [gradient])
	show("offsets", gradient.offsets, PackedFloat32Array([0, 0.25, 1]))
	show("colors", gradient.colors, PackedColorArray([Color(1, 0, 0), Color(0, 1, 0), Color(0, 0, 1)]))
	var dialog := FileDialog.new()
	ask("filter", [dialog])
	show("filters", dialog.filters, PackedStringArray(["*.png", "*.as", ""]))
	dialog.free()
	var area := NavigationPolygon.new()
	ask("outline", [area])
	show("polygons", [area.get_polygon(0), area.get_polygon(1)], [PackedInt32Array([0, 1, 2]), PackedInt32Array([3, 1, 2])])
	var a := Node.new()
	var b := Node.new()
	a.name = "a"
	b.name = "b"
	root.add_child(a)
	root.add_child(b)
	ask("remember", [node, a, b])
	show("whole", node.get_meta("whole"), [1, -2, 3])
	show("whole are integers", typeof(node.get_meta("whole")[0]), TYPE_INT)
	show("fractions", node.get_meta("fractions"), [0.5, 2.0])
	show("fractions are floats", typeof(node.get_meta("fractions")[1]), TYPE_FLOAT)
	show("words", node.get_meta("words"), ["a", "b"])
	show("flags", node.get_meta("flags"), [true, false])
	show("nodes", node.get_meta("nodes"), [a, b, null])
	show("wide", node.get_meta("wide"), [9007199254740993])
	var first := Node2D.new()
	var second := Node2D.new()
	var third := Node2D.new()
	show("keep", [ask("keep", [first]), ask("keep", [second]), ask("keep", [third])], [1, 2, 3])
	show("spread", ask("spread"), 0.0 + 10 + 20 + 0 + 1 + 2)
	show("moved", [first.position, second.position, third.position], [Vector2(0, 0), Vector2(10, 1), Vector2(20, 2)])
	show("words of values", ask("words"), "(1.0, 2.0) (3.0, 4.0)/(1.0, 2.0),(3.0, 4.0)/3")
	show("forget", ask("forget"), 0)
	first.free()
	second.free()
	third.free()
	return true
