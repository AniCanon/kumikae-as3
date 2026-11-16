# The cycle collector and script objects of reference-counted classes (Paired.as, Cycles.as), and of a script
# whose base is Godot's Object put on a RefCounted (Unjoined.as): a loop of one with a plain object goes when
# nothing holds it, and stays whole, fields and all, while GDScript or AS3 does. Whole frames pass between
# the checks, so that collections run between them.
extends SceneTree

var cycles: Node
var frames := 0
var nobody := 0
var through := 0
var static_id := 0
var godot_only: Resource
var kept: Resource
var read_only: WeakRef
var closure_id := 0
var talker: Resource
var talker_gone: WeakRef
var written_only: WeakRef
var let_go: WeakRef
var unjoined_nobody: WeakRef
var unjoined_plain: WeakRef
var unjoined_kept: RefCounted
var unjoined_kept_gone: WeakRef
var unjoined_static_id := 0
var unjoined_object: Object

# A RefCounted with Unjoined.as on it, a script whose base is Godot's Object.
func unjoined(label: String, through: bool) -> RefCounted:
	var made := RefCounted.new()
	made.set_script(load("res://Unjoined.as"))
	made.loop(label, through)
	return made

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
	closure_id = cycles.closureLoop()
	talker = cycles.closureHeld()
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
	var attached := unjoined("attached", false)
	show("a script whose base is Object goes on a RefCounted", [attached.get_script() != null, attached.word()], [true, "attached partner true"])
	unjoined_nobody = weakref(unjoined("nobody", true))
	unjoined_plain = weakref(unjoined("plain", false))
	unjoined_kept = unjoined("godot", true)
	var held := unjoined("static", false)
	held.hold()
	unjoined_static_id = held.get_instance_id()
	# The same script on an Object, which is not counted: Godot holds it until it is freed.
	unjoined_object = Object.new()
	unjoined_object.set_script(load("res://Unjoined.as"))
	unjoined_object.loop("object", false)

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
		show("a function listening to its own object's signal goes with it", [closure_id != 0, is_instance_id_valid(closure_id)], [true, false])
		show("and stays, still listening, while GDScript holds the object", talker.say(), 1)
		show("on a RefCounted, a loop through Godot's count held by nobody goes", unjoined_nobody.get_ref() == null, true)
		show("on a RefCounted, a loop held by nobody goes", unjoined_plain.get_ref() == null, true)
		show("on a RefCounted, a loop GDScript holds stays whole", unjoined_kept.word(), "godot partner true")
		show("on a RefCounted, what an AS3 static holds stays whole", [is_instance_id_valid(unjoined_static_id), cycles.unjoinedWord()], [true, "static partner true"])
		show("on a RefCounted, what an AS3 static holds keeps its Godot object", cycles.unjoinedIsCounted() if is_instance_id_valid(unjoined_static_id) else false, true)
		unjoined_kept_gone = weakref(unjoined_kept)
		unjoined_kept = null
		cycles.letGoOfUnjoined()
		show("on an Object, a loop GDScript holds stays whole", unjoined_object.word(), "object partner true")
		unjoined_object.hold()
		unjoined_object.free()
		talker_gone = weakref(talker)
		talker = null
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
		show("so does the listening one", talker_gone.get_ref() == null, true)
		show("on a RefCounted, a loop GDScript held goes when it lets go", unjoined_kept_gone.get_ref() == null, true)
		show("on a RefCounted, what a static held goes when the static lets go", is_instance_id_valid(unjoined_static_id), false)
		show("on an Object, what an AS3 static holds stays whole when Godot frees the Object", cycles.unjoinedWord(), "object partner true")
		cycles.letGoOfUnjoined()
		cycles.queue_free()
		return true
	return false
