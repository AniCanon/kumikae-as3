# The cycle collector and script objects of reference-counted classes (Paired.as, Cycles.as): a loop of one
# with a plain object goes when nothing holds it, and stays whole, fields and all, while GDScript or AS3
# does. Whole frames pass between the checks, so that collections run between them.
extends SceneTree

var cycles: Node
var frames := 0
var nobody := 0
var through := 0
var static_id := 0
var godot_only: Resource
var kept: Resource
var read_only: WeakRef
var written_only: WeakRef
var let_go: WeakRef

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func _initialize() -> void:
	cycles = Node.new()
	cycles.set_script(load("res://Cycles.as"))
	root.add_child(cycles)
	nobody = cycles.nobody()
	through = cycles.throughGodot()
	static_id = cycles.staticOnly()
	godot_only = cycles.godotOnly()
	kept = cycles.keptByGodot()
	# Made by GDScript and read by a getter that keeps it in a static: AS3 holds it now, and it outlives
	# GDScript's hold.
	var made := Resource.new()
	made.set_script(load("res://Paired.as"))
	made.set("label", "read")
	show("a getter read", made.get("keeping"), "read")
	read_only = weakref(made)
	var written := Resource.new()
	written.set_script(load("res://Paired.as"))
	written.set("keeper", "written")
	written_only = weakref(written)

func _physics_process(_delta: float) -> bool:
	frames += 1
	if frames == 60:
		show("a loop held by nobody goes", is_instance_id_valid(nobody), false)
		show("a loop through Godot's count goes", is_instance_id_valid(through), false)
		show("a loop GDScript holds stays whole", [godot_only.get("label"), godot_only.partnerWord()], ["godot", "partner true"])
		show("a loop an AS3 static holds stays whole", [is_instance_id_valid(static_id), cycles.staticWord()], [true, "static partner true"])
		show("kept by Godot alone stays whole", [kept.get("label"), kept.partnerWord()], ["kept", "partner false"])
		show("what a getter kept outlives GDScript's hold", [read_only.get_ref() != null, cycles.readWord()], [true, "read no partner"])
		show("what a setter kept outlives GDScript's hold", [written_only.get_ref() != null, cycles.writtenWord()], [true, "written"])
		cycles.letGoOfStatic()
		cycles.letGoOfRead()
		let_go = weakref(godot_only)
		godot_only = null
		kept = null
		return false
	if frames == 120:
		show("a static's loop goes when the static lets go", is_instance_id_valid(static_id), false)
		show("what a getter kept goes when AS3 lets go", read_only.get_ref() == null, true)
		show("what a setter kept goes when AS3 lets go", written_only.get_ref() == null, true)
		show("a loop GDScript held goes when it lets go", let_go.get_ref() == null, true)
		cycles.queue_free()
		return true
	return false
