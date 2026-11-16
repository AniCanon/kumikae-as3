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

# A singleton's signal is the whole game's: removeAll() and numListeners on it are about the calling
# script's own listeners, its object's methods (and its class's static methods), never another script's,
# another object's of the same class, GDScript's or a function expression's.
func singleton_remove_all() -> void:
	var second := Node2D.new()
	second.set_script(load("res://Statics.as"))
	root.add_child(second)
	var other := Node.new()
	other.set_script(load("res://JoyListener.as"))
	root.add_child(other)
	var connected := Input.joy_connection_changed.get_connections().size()
	Input.joy_connection_changed.connect(on_joy)
	heard = 0
	node.joys = 0
	var statics: int = ask("heardStatic")
	ask("listen")
	second.listen()
	other.listen()
	ask("listenInPlace")
	show("a singleton's listeners counted by their own script", [ask("listeners"), second.listeners(), other.listeners()], [2, 2, 1])
	ask("clear")
	Input.joy_connection_changed.emit(1, true)
	show("removeAll() on a singleton's signal takes the caller's own",
		[node.joys, second.joys, other.joys, heard, ask("heardStatic") - statics], [10, 1, 1, 1, 0])
	show("and leaves the others' counted", [ask("listeners"), second.listeners(), other.listeners()], [0, 1, 1])
	second.listen()
	ask("listen")
	other.call("clear")
	Input.joy_connection_changed.emit(1, true)
	show("from another class, it takes that class's own", [node.joys, second.joys, other.joys, heard], [21, 2, 1, 2])
	ask("listenStatic")
	ask("clearFromStatic")
	Input.joy_connection_changed.emit(1, true)
	show("from static code, its class's static listeners", [node.joys, second.joys, ask("heardStatic") - statics], [32, 3, 0])
	ask("listenStatic")
	ask("clearInPlace")
	Input.joy_connection_changed.emit(1, true)
	show("from a function expression, which has no object, the class's static listeners", [node.joys, ask("heardStatic") - statics], [43, 0])
	ask("unlistenInPlace")
	ask("clear")
	second.clear()
	Input.joy_connection_changed.disconnect(on_joy)
	show("nothing is left listening", Input.joy_connection_changed.get_connections().size(), connected)
	second.free()
	other.free()

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
	show("an editor-only singleton is an AS3 error", ask("editorOnly"), true)
	show("a class's static method", ask("staticWorks"), JSON.stringify(3))
	show("orphan nodes printed", ask("orphansShown"), true)
	ask("fakeTrace")
	show("a static call's arguments in order", ask("staticOrder"), "ab")
	show("the value before the missing object", ask("writeMissing"), "value caught")
	show("a static call's result of the wrong type", ask("staticWrongType"), "caught")
	show("a singleton's members through a variable", ask("viaObject"), true)
	show("a singleton variable that is null", ask("viaNull"), true)
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
	var before = node.joys
	ask("listenInPlace")
	Input.joy_connection_changed.emit(2, true)
	show("a function written in place listens", [node.joys - before, ask("heardStatic")], [20, 2])
	ask("unlistenInPlace")
	Input.joy_connection_changed.emit(5, true)
	show("and is removed through its value", [node.joys - before, ask("heardStatic")], [20, 2])
	Input.joy_connection_changed.disconnect(on_joy)
	singleton_remove_all()
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
	var seen := []
	var task := WorkerThreadPool.add_task(func(): node.set("shared", 5); seen.append(node.get("shared")))
	WorkerThreadPool.wait_for_task_completion(task)
	show("a field from another thread is refused", [seen, node.get("shared")], [[null], 1])
	node.free()
	return false
