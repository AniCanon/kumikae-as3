extends SceneTree

# Calls each method of Probe.as, of Entry.as and of the test project's Places.as, Loops.as and Emits.as and prints what
# it gives: run_godot.py runs this with the interpreter and with the scripts as native code, and holds the two
# outputs to each other.
var killer: Node
var entry: Node2D
var emits: RefCounted
var frames := 0
var waiting: Node2D


# Calls back into the script as it is freed (Probe.as, clears()).
class Dying extends RefCounted:
	var target
	func _notification(what: int) -> void:
		if what == NOTIFICATION_PREDELETE and target != null:
			target.call("dies")


# Godot calling into a script (Entry.as).
func _enter() -> void:
	entry = Node2D.new()
	entry.set_script(load("res://Entry.as"))
	root.add_child(entry)
	print("PROBE defaults=", entry.call("defaults", 1), "/", entry.call("defaults", 1, 2), "/", entry.call("defaults", 1, 2, "three", 4.0))
	print("PROBE rest=", entry.call("rest", 1), "/", entry.call("rest", 1, 2, "three"))
	for what in 6:
		print("PROBE gives ", what, "=", entry.call("gives", what) if what != 1 else entry.call("gives", what) == entry)
	print("PROBE takes=", entry.call("takes", Vector2(3, 4), entry, "text", [1, 2]), "/", entry.call("takes", Vector2(), null, "", null))
	print("PROBE nested=", entry.call("nested", 5), "/", entry.call("nested", 40), "/", entry.call("nested", 100))
	entry.call("listen")
	print("PROBE unheard=", entry.call("unheard"))
	print("PROBE both=", entry.call("both"))
	print("PROBE rings=", entry.call("rings"))
	print("PROBE rings again=", entry.call("rings"))


# Vectors whose places hold Godot's values themselves (the test project's Places.as, which places.gd holds to
# what GDScript gives).
func _places() -> void:
	var places := Node2D.new()
	places.set_script(load("res://Places.as"))
	places.position = Vector2(100, 200)
	root.add_child(places)
	print("PROBE grown=", places.call("grown"))
	print("PROBE refused=", places.call("refused"))
	print("PROBE methods=", places.call("methods"))
	print("PROBE ends=", places.call("ends"))
	var line := Line2D.new()
	var keeper := Node.new()
	print("PROBE given=", places.call("given", line, keeper), " ", line.points, " ", keeper.get_meta("points"), " ", keeper.get_meta("nested"), " ",
			keeper.get_meta("none"), " ", keeper.get_meta("cells"))
	var shade := Gradient.new()
	print("PROBE wider=", places.call("wider", shade), " ", shade.colors)
	line.position = Vector2(-1, -2)
	print("PROBE taken=", places.call("taken", line, [Vector2(0.5, 0.25), Vector3(1, 2, 3), null]))
	line.free()
	keeper.free()


# Loops over Godot's collections (the test project's Loops.as, which loops.gd holds to what GDScript gives).
func _loops() -> void:
	var loops := Node2D.new()
	loops.set_script(load("res://Loops.as"))
	root.add_child(loops)
	for child_name in ["first", "second"]:
		var child := Node.new()
		child.name = child_name
		loops.add_child(child)
	var table := {"a": 1, 2: "two", "c": Vector2(3, 4)}
	var growing := [1, 2]
	print("PROBE loops=", loops.call("childNames"), " ", loops.call("sum", [1, 2.5, 4]), " ", loops.call("untypedElements", [7, "s", true, Vector2(1, 2), null]),
			" ", loops.call("indices", [4, 5]), " ", loops.call("keys", table), " ", loops.call("textKeys", {"x": 1}), " ", loops.call("values", table),
			" ", loops.call("untilNegative", [3, 0, 4, -1, 5]), " ", loops.call("grown", growing), growing, " ", loops.call("erasedOnTheWay", {"a": 1, "b": 2, "c": 3}),
			" ", loops.call("pairs", [1, 2, 3]), " ", loops.call("packedIndices", PackedInt32Array([5, 6])))
	print("PROBE keyed=", loops.call("has", table, 2), loops.call("has", table, "2"), " ", loops.call("remove", table, 2), table, " ", loops.call("removeNamed", {"b": 1}),
			" ", loops.call("wrongElement", [loops, 5]))


# What signals carry between AS3 and Godot (the test project's Emits.as, by the rows emits.gd holds to what
# GDScript gives): what AS3 gave for each.
func _emits_said(from: int) -> void:
	for at in range(from, emits.rows.size()):
		print("PROBE emits ", emits.rows[at][0], "=", emits.rows[at][1])


func _initialize() -> void:
	var probe := Node2D.new()
	probe.set_script(load("res://Probe.as"))
	root.add_child(probe)
	print("PROBE vectors=", probe.call("vectors"))
	print("PROBE values=", probe.call("values"))
	print("PROBE fields=", probe.call("fields"))
	print("PROBE given=", probe.call("given"))
	print("PROBE carried=", probe.call("carried"))
	print("PROBE engine=", probe.call("engine"))
	print("PROBE field objects=", probe.call("fieldObjects"))
	print("PROBE in handles=", probe.call("inHandles"))
	var dying := Dying.new()
	dying.target = probe
	print("PROBE holds=", probe.call("holds", dying))
	dying = null
	print("PROBE clears=", probe.call("clears"))
	print("PROBE script=", probe.call("script", 1000))
	print("PROBE script again=", probe.call("script", 10))
	print("PROBE listeners=", probe.call("listeners"))
	print("PROBE speaks=", probe.call("speaks"))
	print("PROBE gone=", probe.call("gone"))
	print("PROBE gone uncaught=", probe.call("goneUncaught"))
	print("PROBE queued=", probe.call("queue"))
	waiting = probe
	print("PROBE fail=", probe.call("fail", 3))
	print("PROBE after=", probe.call("script", 1))
	_enter()
	_places()
	_loops()
	emits = load("res://emits_cases.gd").new()
	emits.begin(root)
	_emits_said(0)
	# A node whose script's object is freed while its _process runs: see Probe.as.
	killer = Node.new()
	killer.set_script(load("res://killer.gd"))
	root.add_child(killer)
	var victim := Node2D.new()
	victim.set_script(load("res://Probe.as"))
	root.add_child(victim)
	killer.set("victim", victim)
	victim.set("killer", killer)


func _process(_delta: float) -> bool:
	frames += 1
	if frames < 4:
		return false
	print("PROBE freed=", killer.get("said") if killer else "no killer")
	print("PROBE after the frame=", waiting.call("afterQueue"))
	print("PROBE ticked=", entry.call("ticked"))
	var said: int = emits.rows.size()
	emits.finish()
	_emits_said(said)
	return true
