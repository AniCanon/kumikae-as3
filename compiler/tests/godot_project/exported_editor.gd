# [Export] in the editor (run with --editor): no AS3 runs there, and the object Godot edits holds the
# exported fields, starting at their first values, as GDScript's stand-in does.
extends SceneTree

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func _initialize() -> void:
	show("in the editor", Engine.is_editor_hint(), true)
	var node := Node2D.new()
	node.set_script(load("res://Exported.as"))
	show("no AS3 ran", node.name, "")
	show("first values", [node.get("speed"), node.get("lives"), node.get("title"), node.get("spot")], [5.0, 3, "hero", Vector2.ZERO])
	node.set("speed", 9.5)
	show("a value the inspector set", node.get("speed"), 9.5)
	var packed := PackedScene.new()
	packed.pack(node)
	var state := packed.get_state()
	var stored := []
	for i in state.get_node_property_count(0):
		stored.append(String(state.get_node_property_name(0, i)))
	stored.sort()
	show("what the editor saves", stored, ["script", "speed"])
	var scene: Node2D = load("res://exported.tscn").instantiate()
	show("a scene opened in the editor", [String(scene.name), scene.get("speed"), scene.get("spot")], ["Exported", 12.5, Vector2(3, 4)])
	scene.free()
	node.free()
	quit()
