# Godot's values and objects held untyped in AS3, used by name. Each line shows what the AS3 script gave and
# what GDScript gives for the same thing with an untyped variable; run_godot.py checks the two agree.
extends SceneTree

class Intercept extends Node2D:
	var reads := 0
	var writes := 0
	var stored := true
	var allocation_probe := 7

	func script_score() -> int:
		return 41

	func _get(property: StringName):
		if property.begins_with("allocation_probe_rotating_") or property.begins_with("cold_field_"):
			return 1
		if property == &"visible":
			reads += 1
			return stored
		return null

	func _set(property: StringName, value) -> bool:
		if property == &"visible":
			writes += 1
			stored = value
			return true
		return false

class CallableField extends Node2D:
	var action: Callable = offset.bind(3)

	func offset(n: int, extra: int) -> int:
		return n + extra

class CallableMethod extends Node2D:
	func action(n: int) -> int:
		return n * 2

class SchemaEditor extends RefCounted:
	func invoke(cb: Callable) -> int:
		return cb.call(7)

	func replace_schema(receiver: Node2D) -> void:
		var script: GDScript = receiver.get_script()
		script.source_code = receiver.get_meta("next")
		assert(script.reload(true) == OK)

class VirtualOnly extends Node2D:
	var reads := 0

	func _get(property: StringName):
		if property == &"stored":
			reads += 1
			return "virtual"
		return null

class InterceptTimer extends Timer:
	func _get(property: StringName):
		if property == &"wait_time":
			return 7.5
		return null

class Positions extends Node2D:
	var reads := 0
	var writes := 0

	func _get(property: StringName):
		if property == &"position":
			reads += 1
			if reads == 5:
				return "wrong type"
			return Vector2(reads, 4)
		return null

	func _set(property: StringName, _value) -> bool:
		if property == &"position":
			writes += 1
			return true
		return false

var node: Node2D
var child: Node2D
var scripted: Node2D
var frames := 0

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func ask(method: String, arguments: Array = []):
	return Callable(node, method).callv(arguments)

func fresh() -> Node2D:
	if child:
		child.free()
	child = Node2D.new()
	child.name = "child"
	node.add_child(child)
	return child

func writes(o) -> Array:
	o.position = Vector2(3, 4)
	o.visible = false
	o["name"] = "renamed"
	var at = o.position
	at.x += 1
	o.position = at
	return [o.position, o.visible, str(o.name)]

func calls(o) -> Array:
	o.show()
	var inner = Node.new()
	o.add_child(inner)
	inner.name = "inner"
	return [o.visible, o.get_child_count(), str(o.get_child(0).name), o.is_ancestor_of(inner), o.get_node("inner") == inner]

func of_value() -> Array:
	var v = Vector2(3, 4)
	var color = Color(1, 0.5, 0)
	return [v.x, v.y, v.length(), v.normalized().y, color.g, color.lightened(0.5).r, v is Vector2, "a copy"]

func check_removed_field() -> void:
	var next := "extends Node2D\nvar added := 9\n"
	for mode in ["callback", "entry", "reload", "source"]:
		var live := GDScript.new()
		live.source_code = "extends Node2D\nvar stored := 4\n"
		assert(live.reload() == OK)
		var receiver := Node2D.new()
		receiver.set_script(live)
		receiver.set_meta("next", next)
		var editor := SchemaEditor.new()
		var result
		if mode == "callback":
			result = ask("removedDuringCall", [receiver, "stored", editor])
		elif mode == "entry":
			var before = ask("writeKey", [receiver, "stored"])
			editor.replace_schema(receiver)
			result = [before, ask("writeKey", [receiver, "added"])]
		else:
			live.source_code = next
			result = ask("writeAcrossReload", [receiver, "stored", live, editor, next if mode == "source" else null])
		if mode != "callback":
			receiver.set("added", 17)
		var current = receiver.get("stored" if mode == "callback" else "added")
		show("reload classification " + mode, result, [17, 1069 if mode == "callback" and current == null else current])
		receiver.free()

func _initialize() -> void:
	node = Node2D.new()
	node.set_script(load("res://ByName.as"))
	node.name = "main"
	root.add_child(node)
	scripted = Node2D.new()
	scripted.set_script(load("res://ByName.as"))
	scripted.name = "other"
	root.add_child(scripted)

func _physics_process(_delta: float) -> bool:
	frames += 1
	if frames < 2:
		return false
	if frames > 2:
		return true
	var o = fresh()
	show("properties read by name", ask("reads", [o]), [str(o.name), str(o["name"]), o.visible, o.position, o.get_child_count(), o.get_parent() == node, true])
	show("properties written by name", ask("writes", [fresh()]), writes(fresh()))
	show("methods called by name", ask("calls", [fresh()]), calls(fresh()))
	show("a name that is not there", ask("missing", [fresh()]), [
			"Error #1069: Property nothing not found on Object and there is no default value.",
			"Error #1056: Cannot create property nothing on Object.", "Error #1006: nothing is not a function.", 1037, 1006, "refused", "refused"])
	o = fresh()
	show("in and hasOwnProperty", ask("has", [o]), ["name" in o, "nothing" in o, o.has_method("hide"), o.has_method("nothing")])
	show("a value's parts and methods", ask("ofValue"), of_value())
	show("a node that carries a script", ask("scripted", [scripted]), [9, 8, "other", false, true, true, true])
	show("the script's own object", ask("self"), [5, 6, "main", 0.5, true, node.get_child_count()])
	o = fresh()
	show("a method read by name", ask("method", [o]), [false, true, true, "function"])
	show("a signal reached by name", ask("listens", [fresh()]), [true, 1, 0])
	show("exact untyped arguments", ask("exactArguments", [fresh()]), [false, Vector2(6, 8), 0.5, 100.0])
	var timer := Timer.new()
	var initial = timer.get("wait_time")
	timer.set_script(InterceptTimer)
	show("fresh timer after attaching a script", ask("attachedTimer", [InterceptTimer]), initial + timer.get("wait_time"))
	timer.free()
	var foreign := Intercept.new()
	show("foreign declarations", ask("foreignDeclarations", [foreign, "stored", "script_score"]),
		[foreign.stored, foreign.script_score(), foreign.get("stored"), foreign.call("script_score")])
	show("warmed script field", ask("readMany", [foreign]), 112)
	show("rotating computed names", ask("rotatingKeys", [foreign]), 192)
	for receiver in [CallableField.new(), CallableMethod.new()]:
		var is_method: bool = receiver.has_method("action")
		var sink := SchemaEditor.new()
		show("method value" if is_method else "Callable field", ask("actionValues", [receiver, "action", sink]),
				[is_method, is_method, sink.invoke(receiver.get("action")), sink.invoke(receiver["action"])])
		receiver.free()
	check_removed_field()
	var controls := {}
	for managed in [false, true]:
		var changing := Node2D.new()
		changing.set_script(Intercept)
		var declared = ask("readStored", [changing]) if managed else changing.get("stored")
		changing.set_script(VirtualOnly)
		var removed = ask("readStored", [changing]) if managed else changing.get("stored")
		var replacement := [declared, removed, changing.get("reads")]
		changing.free()
		var live := GDScript.new()
		var next := "extends Node2D\nvar reads := 0\nfunc _get(key):\n if key == &'stored':\n  reads += 1\n  return 'virtual'\n return null\n"
		for typed in [false, true]:
			live.source_code = next + "var stored := true\n"
			assert(live.reload() == OK)
			var edited := Node2D.new()
			edited.set_script(live)
			edited.set_meta("next", next)
			var observed
			if typed:
				live.source_code = next
			if managed:
				observed = ask("readAcrossTypedReload" if typed else "readAcrossReload",
						[edited, "stored", live if typed else SchemaEditor.new()])
			else:
				var before = edited.get("stored")
				var had := live.get_script_property_list().any(func(p): return p.name == "stored")
				if typed:
					assert(live.reload(true) == OK)
				else:
					SchemaEditor.new().replace_schema(edited)
				observed = [before, had, edited.get("stored"),
						live.get_script_property_list().any(func(p): return p.name == "stored")]
			var label := "typed reload after staging source" if typed else "foreign declaration after in-place reload"
			var result := [observed, edited.get("reads")]
			if managed:
				show(label, result, controls[label])
			else:
				controls[label] = result
			edited.free()
		if managed:
			show("foreign declaration after replacement", replacement, controls.replacement)
		else:
			controls.replacement = replacement
	var reference := Intercept.new()
	reference.set("visible", false)
	var expected = [reference.get("visible"), reference.reads, reference.writes]
	var seen = ask("foreignVisible", [foreign])
	show("foreign script accessors", [seen, foreign.reads, foreign.writes], expected)
	var control := Control.new()
	show("same site on alternating classes and scripts", [
		ask("foreignVisible", [control]), ask("foreignVisible", [scripted]), ask("foreignVisible", [foreign]),
		ask("foreignVisible", [control]), foreign.reads, foreign.writes], [false, false, false, false, 2, 2])
	var replaced := Node2D.new()
	var before = ask("foreignVisible", [replaced])
	replaced.set_script(Intercept)
	var after = ask("foreignVisible", [replaced])
	var counts := [replaced.get("reads"), replaced.get("writes")]
	replaced.set_script(null)
	show("replacement on the same receiver", [before, after, counts, ask("foreignVisible", [replaced])],
			[false, false, [1, 1], false])
	replaced.set_script(Intercept)
	show("parameter alias keeps foreign accessors", [ask("aliasVisible", [replaced]), replaced.get("reads"), replaced.get("writes")],
			[false, 1, 1])
	show("computed misses", ask("missingKeys", [control, 256]), 0)
	show("null math receivers", ask("nullParts"), [1009, 1009])
	var positions := Positions.new()
	show("parameter alias respects position overrides", [ask("aliasPositions", [positions]), positions.reads, positions.writes], [[3, true], 5, 1])
	positions.reads = 0
	show("computed getter values keep their types and copies", ask("computedPositions", [positions, "position"]),
			[Vector2(1, 4), Vector2(2, 4), Vector2(3, 4), Vector2(4, 4), "wrong type"])
	positions.free()
	replaced.free()
	control.free()
	foreign.free()
	reference.free()
	var warmed := Node2D.new()
	warmed.name = "warmed"
	show("freed after warming the site", ask("freeAfterRead", [warmed]), "warmed/1009")
	var gone := Node2D.new()
	var held = gone
	gone.free()
	show("a freed object", ask("freed", [held]), "TypeError 1009")
	child.free()
	scripted.free()
	node.free()
	return false
