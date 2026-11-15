# AS3's Objects and Dictionaries between AS3 and Godot. Each line shows what the AS3 script gave and what
# GDScript gives for the same thing; run_godot.py checks the two agree.
extends SceneTree

var node: Node2D
var frames := 0

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func ask(method: String, arguments: Array = []):
	return Callable(node, method).callv(arguments)

func _initialize() -> void:
	node = Node2D.new()
	node.set_script(load("res://Objects.as"))
	root.add_child(node)

func _physics_process(_delta: float) -> bool:
	frames += 1
	if frames < 2:
		return false
	if frames > 2:
		return true
	ask("literalToGodot", [node])
	show("an Object is a Dictionary", node.get_meta("o"), {"a": 1, "b": "two", "list": [1, {"c": 2.5}], "at": Vector2(1, 2)})
	ask("dictionaryToGodot", [node])
	show("a Dictionary is a Dictionary, its keys text", node.get_meta("d"), {"x": 1, "2": "two"})
	ask("insideArray", [node])
	show("Objects inside an Array", node.get_meta("list"), [{"k": "v"}, [{"n": null}]])
	show("an Object returned", ask("made"), {"made": true, "count": 3})
	show("an Object where Godot takes a Dictionary", ask("queried"), HTTPClient.new().query_string_from_dict({"a": 1, "b": "x y"}))
	show("a Dictionary from Godot", ask("fromGodot", [{"a": 1, 2: "b"}]), "a=1;2=b;")
	node.set_meta("table", {"name": "t"})
	show("Godot's Dictionary as a Dictionary", ask("stored", [node]), "t/true/true")
	show("a loop over Godot's Dictionary held untyped is not run", ask("loopGodot", [node]), null)
	ask("holdsItself", [node])
	show("an Object that holds itself is not given", node.has_meta("loop"), false)
	ask("withProperties", [node])
	show("an Array with properties is not given", node.has_meta("props"), false)
	ask("plainInside", [node])
	show("a script object inside an Object is not given", node.has_meta("plain"), false)
	return false
