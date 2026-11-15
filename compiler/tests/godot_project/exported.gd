# [Export]: a scene's saved values reach the script, Godot is told each field's first value, and a scene
# saved again keeps only what differs from it. Each line shows what Godot gave and what it should give.
extends SceneTree

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func stored(packed: PackedScene) -> Array:
	var state := packed.get_state()
	var out := []
	for i in state.get_node_property_count(0):
		out.append(String(state.get_node_property_name(0, i)))
	out.sort()
	return out

func _initialize() -> void:
	var node: Node2D = load("res://exported.tscn").instantiate()
	root.add_child(node)
	show("the scene's values", node.call("describe"), "12.5 3 hero true (3.0, 4.0) 1+2+3 -1.5 (1.0, 2.0) texture 4x8 Child")

	var script: Script = node.get_script()
	var shown := []
	for property in script.get_script_property_list():
		if property["usage"] & PROPERTY_USAGE_EDITOR:
			shown.append(property["name"])
	show("shown in the inspector", shown, ["speed", "lives", "title", "alive", "spot", "items", "below", "made", "offset", "tint", "texture", "target"])
	var usage := 0
	var hint := ""
	var node_hint := ""
	for property in script.get_script_property_list():
		if property["name"] == "speed":
			usage = property["usage"]
		if property["name"] == "texture":
			hint = "%d %s" % [property["hint"], property["hint_string"]]
		if property["name"] == "target":
			node_hint = "%d %s" % [property["hint"], property["hint_string"]]
	show("stored and shown", usage, PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_SCRIPT_VARIABLE)
	show("a resource's hint", hint, "%d Texture2D" % PROPERTY_HINT_RESOURCE_TYPE)
	show("a node's hint", node_hint, "%d Node" % PROPERTY_HINT_NODE_TYPE)
	var defaults := []
	for name in ["speed", "lives", "title", "alive", "spot", "below"]:
		defaults.append([script.get_property_default_value(name), typeof(script.get_property_default_value(name))])
	show("first values", defaults, [[5.0, TYPE_FLOAT], [3, TYPE_INT], ["hero", TYPE_STRING], [true, TYPE_BOOL],
			[Vector2.ZERO, TYPE_VECTOR2], [-1.5, TYPE_FLOAT]])
	show("one of Godot's values made from numbers", [script.get_property_default_value("made"), script.get_property_default_value("offset"),
			script.get_property_default_value("tint")], [Vector2(1, 2), Vector2i(-1, 3), Color(0.5, 0, 1)])
	show("a field that is not exported has none", script.get_property_default_value("notExported"), null)

	# Saved again, a scene keeps the values that differ from the first ones.
	var packed := PackedScene.new()
	packed.pack(node)
	show("saved again", stored(packed), ["items", "script", "speed", "spot", "target", "texture"])
	node.set("texture", null)
	node.set("target", null)
	show("a resource and a node let go", node.call("describe").ends_with("no texture no target"), true)
	packed.pack(node)
	node.set("speed", 5.0)
	node.set("lives", 4)
	packed.pack(node)
	show("saved after a change", stored(packed), ["items", "lives", "script", "spot"])

	node.free()
	quit()
