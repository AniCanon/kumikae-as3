# Singletons, constants and static methods in AS3. Each line shows what the AS3 script gave and what
# GDScript gives for the same thing; run_godot.py checks the two agree.
extends SceneTree

var node: Node2D
var frames := 0
var heard := 0

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func ask(method: String, arguments: Array = []):
	return Callable(node, method).callv(arguments)

func on_joy(_device: int, _connected: bool) -> void:
	heard += 1

func _initialize() -> void:
	node = Node2D.new()
	node.set_script(load("res://Statics.as"))
	root.add_child(node)

func _physics_process(_delta: float) -> bool:
	frames += 1
	if frames < 2:
		return false
	if frames > 2:
		return true
	# Singletons.
	show("pressed", ask("pressed", ["ui_accept"]), Input.is_action_pressed("ui_accept"))
	show("strength", ask("strength", ["ui_accept"]), Input.get_action_strength("ui_accept"))
	show("stick", ask("stick"), Input.get_vector("ui_left", "ui_right", "ui_up", "ui_down"))
	show("system", ask("system"), OS.get_name())
	ask("slow", [0.5])
	show("slowed", Engine.time_scale, 0.5)
	show("time scale", ask("timeScale"), Engine.time_scale)
	show("faster", ask("faster"), 1.0)
	show("faster in Godot", Engine.time_scale, 1.0)
	show("at start", ask("atStart"), Engine.physics_ticks_per_second)
	show("mouse", ask("mouse"), Input.mouse_mode)
	Input.joy_connection_changed.connect(on_joy)
	ask("listen")
	ask("listen")
	show("listening", ask("listening"), true)
	Input.joy_connection_changed.emit(3, true)
	show("heard", node.joys, 3)
	ask("clear")
	Input.joy_connection_changed.emit(3, true)
	show("cleared", node.joys, 3)
	show("others still listen", heard, 2)
	ask("listen")
	ask("unlisten")
	show("not listening", ask("listening"), false)
	Input.joy_connection_changed.disconnect(on_joy)
	show("input class", ask("inputClass"), Input.get_class())
	show("same singleton", ask("sameSingleton"), true)
	show("singleton object", ask("singletonObject") == Input, true)
	# Constants.
	show("escape", ask("escape"), KEY_ESCAPE)
	show("captured", ask("captured"), Input.MOUSE_MODE_CAPTURED)
	show("ready notice", ask("readyNotice"), Node.NOTIFICATION_READY)
	show("axis", ask("axis"), Vector2.AXIS_Y)
	show("zero", ask("zero"), Vector2.ZERO)
	show("up", ask("up"), Vector2.UP)
	show("red", ask("red"), Color.RED)
	show("forward", ask("forward"), Vector3.FORWARD)
	show("identity", ask("identity"), Transform2D.IDENTITY)
	show("sum", ask("sum"), Vector2.UP * 2 + Vector2.ONE)
	show("up y", ask("upX"), Vector2.UP.y)
	show("default key", ask("keyOr"), KEY_ESCAPE)
	show("given key", ask("keyOr", [KEY_TAB]), KEY_TAB)
	# Static methods.
	show("from angle", ask("fromAngle", [1.25]), Vector2.from_angle(1.25))
	show("hsv", ask("hsv", [0.5, 1.0, 0.75]), Color.from_hsv(0.5, 1.0, 0.75))
	show("html", ask("html", ["#ff8000"]), Color.html("#ff8000"))
	show("euler", ask("euler", [Vector3(0.1, 0.2, 0.3)]), Quaternion.from_euler(Vector3(0.1, 0.2, 0.3)))
	show("image", ask("image"), 42)
	ask("orphans")
	show("after orphans", true, true)
	# Zero values.
	show("unset", ask("unset"), Vector2())
	show("unset tint", ask("unsetTint"), Color())
	show("unset spot", ask("unsetSpot"), str(Vector2()))
	show("unset list", ask("unsetList"), true)
	show("cleared local", ask("cleared"), true)
	show("declared twice", ask("twice", [false]), Vector2())
	show("declared twice, value first", ask("twice", [true]), Vector2.ONE)
	show("declared twice, other order", ask("twiceBack", [true]), Vector2())
	show("declaration skipped", ask("skipped", [false]), Vector2())
	show("field read early", ask("earlyField"), Vector2())
	show("static read early", ask("earlyStaticField"), Vector2())
	# Worker threads.
	show("race", ask("race"), 200000)
	node.free()
	return false
