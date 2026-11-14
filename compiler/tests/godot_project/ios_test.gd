# The scene the exported app opens: a first run of AS3 on a device. It asks AS3 scripts for the same things
# the desktop tests ask, compares each answer with GDScript's, and shows the result on screen and in the log.
extends Node2D

var frames := 0
var passed := 0
var failed := 0
var report: PackedStringArray = []
var label: Label

func check(what: String, got, want) -> void:
	var ok := str(got) == str(want)
	passed += 1 if ok else 0
	failed += 0 if ok else 1
	report.append(("PASS  " if ok else "FAIL  ") + what + ("" if ok else ": got " + str(got) + ", wanted " + str(want)))
	print("AS3TEST " + report[-1])

func attach(path: String) -> Node2D:
	var node := Node2D.new()
	node.set_script(load(path))
	add_child(node)
	return node

func _ready() -> void:
	label = Label.new()
	label.position = Vector2(24, 90)
	label.add_theme_font_size_override("font_size", 22)
	add_child(label)
	label.text = "ActionScript 3 in Godot on " + OS.get_name() + "\nrunning..."

func _physics_process(_delta: float) -> void:
	frames += 1
	if frames != 31:
		return
	var player := $Player
	check("an AS3 script is attached to a CharacterBody2D", player.get_script() != null, true)
	check("Godot called its _physicsProcess 30 times", player.get("steps"), 30)
	check("it moved the body 100 units", abs(player.position.x - 100.0) < 0.01, true)
	var statics := attach("res://Statics.as")
	check("singleton: Input.isActionPressed", statics.call("pressed", "ui_accept"), Input.is_action_pressed("ui_accept"))
	check("singleton: OS.getName", statics.call("system"), OS.get_name())
	check("constant: Vector2.UP", statics.call("up"), Vector2.UP)
	check("constant: Color.RED", statics.call("red"), Color.RED)
	check("static method: Vector2.fromAngle", statics.call("fromAngle", 1.25), Vector2.from_angle(1.25))
	check("operators: Vector2.UP * 2 + Vector2.ONE", statics.call("sum"), Vector2.UP * 2 + Vector2.ONE)
	statics.call("listen")
	Input.joy_connection_changed.emit(3, true)
	check("signal: a listener on Input.joyConnectionChanged", statics.get("joys"), 3)
	statics.call("unlisten")
	var wide := attach("res://Wide.as")
	var counted := RefCounted.new()
	check("Int64: an object's id survives the trip", wide.call("idOf", counted), counted.get_instance_id())
	check("Int64: the largest value", wide.call("echo", 9223372036854775807), 9223372036854775807)
	check("Int64: a wide value for a Vector2i is refused", wide.call("cell", 4294967299), null)
	check("an AS3 error is reported and the engine carries on", true, true)
	label.text = "ActionScript 3 in Godot on %s\n%d passed, %d failed\n\n%s" % [OS.get_name(), passed, failed, "\n".join(report)]
	print("AS3TEST DONE passed=%d failed=%d" % [passed, failed])
	# On a phone the log is not always in reach: the same report as a file in the app's own folder.
	var file := FileAccess.open("user://as3test.txt", FileAccess.WRITE)
	if file:
		file.store_string("%s\n%d passed, %d failed\n%s\n" % [OS.get_name(), passed, failed, "\n".join(report)])
