# What GDScript reaches of a script by name. Each line shows what Godot gave and what it should give, as it
# would for a GDScript script with the same members; run_godot.py checks the two agree.
extends SceneTree

var node: Node2D

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func ask(method: String, arguments: Array = []):
	return Callable(node, method).callv(arguments)

func names(list: Array) -> Array:
	var out := []
	for entry in list:
		out.append(entry["name"])
	out.sort()
	return out

func entry(list: Array, name: String) -> Dictionary:
	for each in list:
		if each["name"] == name:
			return each
	return {}

func _initialize() -> void:
	node = Node2D.new()
	node.set_script(load("res://Members.as"))
	root.add_child(node)

	# Each kind of field is written, converted as a store into it converts.
	node.set("count", 5)
	node.set("ratio", 2)
	node.set("label", "set")
	node.set("flag", true)
	node.set("spot", Vector2(7, 8))
	node.set("wide", 5000000000)
	node.set("small", 4000000000)
	node.set("anything", 2.5)
	show("fields set", ask("describe"), "5 2 set true (7.0, 8.0) 5000000000 4000000000 2.5")
	show("fields read back", [node.get("count"), node.get("ratio"), node.get("label"), node.get("spot")], [5, 2.0, "set", Vector2(7, 8)])
	show("ratio is a float", typeof(node.get("ratio")), TYPE_FLOAT)

	# What does not fit is refused, and the field keeps its value.
	node.set("count", 2.5)
	node.set("count", 5000000000)
	node.set("count", "nine")
	node.set("small", -1)
	node.set("label", 3)
	node.set("spot", Color.RED)
	show("refused", [node.get("count"), node.get("small"), node.get("label"), node.get("spot")], [5, 4000000000, "set", Vector2(7, 8)])
	node.set("count", 6.0)
	show("a whole float into an int", node.get("count"), 6)

	# An Array field takes a copy of Godot's Array, or null.
	var given := [3, 4, 5]
	node.set("items", given)
	given.append(100)
	show("an array is copied", [ask("total"), ask("itemCount")], [12.0, 3])
	node.set("items", null)
	show("an array set to null", ask("itemCount"), -1)
	node.set("items", "no")
	show("an array refuses what is not one", ask("itemCount"), -1)

	var other := Node.new()
	other.name = "other"
	node.set("buddy", other)
	show("a node field", ask("buddyName"), "other")
	node.set("buddy", RefCounted.new())
	show("a node field refuses another class", ask("buddyName"), "other")
	node.set("buddy", null)
	show("a node field set to null", ask("buddyName"), "none")
	other.free()

	# A const is read, never written; a script object can't come from Godot.
	node.set("FIXED", 30)
	show("a const", node.get("FIXED"), 3)
	node.set("mine", node)
	show("a script object field is not set from Godot", node.get("mine"), null)

	# Only public members are reachable.
	for hidden in ["secret", "guarded", "inside", "plain"]:
		node.set(hidden, 100)
	show("hidden fields read", [node.get("secret"), node.get("guarded"), node.get("inside"), node.get("plain")], [null, null, null, null])
	show("hidden fields are not written", ask("hiddenValues"), "7,8,9,10")
	show("hidden methods", [node.has_method("secretMethod"), node.has_method("guardedMethod"), node.has_method("insideMethod"),
			node.has_method("plainMethod")], [false, false, false, false])
	show("public methods", [node.has_method("total"), node.has_method("_ready"), node.has_method("twice")], [true, true, true])

	# Godot is told of the public members, with Godot's types.
	var script: Script = node.get_script()
	var properties: Array = script.get_script_property_list()
	show("the script's properties", names(properties),
			["FIXED", "anything", "buddy", "count", "flag", "items", "label", "mine", "ratio", "small", "spot", "wide"])
	show("the instance's properties", names(node.get_property_list().filter(func(p): return p["usage"] & PROPERTY_USAGE_SCRIPT_VARIABLE)),
			names(properties))
	var types := []
	for name in ["count", "ratio", "label", "flag", "spot", "items", "buddy", "wide", "small", "anything", "FIXED"]:
		types.append(entry(properties, name)["type"])
	show("their types", types, [TYPE_INT, TYPE_FLOAT, TYPE_STRING, TYPE_BOOL, TYPE_VECTOR2, TYPE_ARRAY, TYPE_OBJECT, TYPE_INT, TYPE_INT,
			TYPE_NIL, TYPE_INT])
	show("a node's class", entry(properties, "buddy")["class_name"], &"Node")
	show("not shown or stored", entry(properties, "count")["usage"], PROPERTY_USAGE_SCRIPT_VARIABLE)
	show("untyped is any value", entry(properties, "anything")["usage"] & PROPERTY_USAGE_NIL_IS_VARIANT, PROPERTY_USAGE_NIL_IS_VARIANT)
	var methods: Array = script.get_script_method_list()
	show("the script's methods", names(methods),
			["_ready", "buddyName", "describe", "hiddenValues", "itemCount", "total", "twice"])
	var twice := entry(methods, "twice")
	show("a method's arguments", [twice["args"].map(func(a): return [a["name"], a["type"]]), twice["default_args"].size(), twice["return"]["type"]],
			[[["n", TYPE_INT], ["by", TYPE_FLOAT]], 1, TYPE_FLOAT])
	# Node has a `_ready` of its own in the list too.
	var listed := names(node.get_method_list().filter(func(m): return methods.any(func(s): return s["name"] == m["name"])))
	show("the instance's methods", listed.filter(func(n): return n != "_ready"), names(methods).filter(func(n): return n != "_ready"))
	show("has_method agrees", script.has_method("secretMethod"), false)

	node.free()
	quit()
