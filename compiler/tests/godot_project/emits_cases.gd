extends RefCounted
## The cases of Emits.as, as rows of a name, what the AS3 script gave and what it should have: what GDScript
## gives for the same thing wherever GDScript can do it. emits.gd prints the rows for run_godot.py; the native
## probe (tests/godot_native/probe.gd) prints what AS3 gave, by the interpreter and as native code.

## Hears a signal with whatever it carries, and says each value's type and text.
class Ear extends Node:
	signal sent
	signal struck
	var got: Array = []
	var victim: Node

	func hear(a = "-", b = "-", c = "-", d = "-", e = "-", f = "-") -> void:
		# A node by its name: its number is another in every run.
		got.append(" ".join([a, b, c, d, e, f].map(func(v): return "%d:%s" % [typeof(v), str(v.name) if v is Node else str(v)])))

	func said() -> String:
		var out := ";".join(got)
		got = []
		return out

	func free_it(_amount, _at) -> void:
		got.append("freeing")
		victim.free()


var rows: Array = []
var root: Node
var node: Node
var ear: Ear
var other: Node


func row(name: String, got, want) -> void:
	rows.append([name, got, want])


func fresh(name: String) -> Node:
	var made := Node.new()
	made.set_script(load("res://Emits.as"))
	made.name = name
	root.add_child(made)
	return made


func begin(p_root: Node) -> void:
	root = p_root
	node = fresh("Emitter")
	ear = Ear.new()
	root.add_child(ear)
	other = Node.new()
	other.name = "Other"
	other.add_user_signal("hit")
	root.add_child(other)
	_emitted()
	_other_signals()
	_heard()
	_heard_otherwise()
	_own()
	_lifetimes()


## After a frame has ended: what a listener added to be called later heard.
func finish() -> void:
	row("a listener called later hears what was emitted", node.said(), "4 %s" % Vector2(4, 1))
	node.forget()
	node.queue_free()
	ear.queue_free()
	other.queue_free()


## What AS3's emit() hands a listener, beside what GDScript's hands the same listener.
func _emitted() -> void:
	node.sent.connect(ear.hear)
	ear.sent.connect(ear.hear)
	node.send(3, 2.0, true, Vector2(1.5, -2))
	var got := ear.said()
	ear.sent.emit(3, 2.0, true, Vector2(1.5, -2))
	row("emit: an int, a Number, a Boolean and a Vector2", got, ear.said())
	node.sendWhole(4000000000, 5000000000, -7)
	got = ear.said()
	ear.sent.emit(4000000000, 5000000000, -7, -6.5)
	row("emit: a uint, an Int64 and a sum", got, ear.said())
	node.sendParts(Color(0.1, 0.2, 0.3, 0.4), Transform2D(0.5, Vector2(3, 4)), Vector3(1, 2, 3), Vector2i(5, -6))
	got = ear.said()
	ear.sent.emit(Color(0.1, 0.2, 0.3, 0.4), Transform2D(0.5, Vector2(3, 4)), Vector3(1, 2, 3), Vector2i(5, -6))
	row("emit: Godot's values with parts", got, ear.said())
	node.sendMade()
	got = ear.said()
	for i in 3:
		ear.sent.emit(i, i * 0.5, i == 1, Vector2(i, -i))
	row("emit: values made where they are emitted, in a loop", got, ear.said())
	node.sendHeld("text", other, [1, "two"], null)
	node.sendHeld("", null, 2.0, Vector2(1, 1))
	got = ear.said()
	ear.sent.emit("text", other, [1, "two"], null)
	ear.sent.emit("", null, 2.0, Vector2(1, 1))
	row("emit: a String, a node, an untyped value and no Vector2", got, ear.said())
	node.sendNothing()
	node.sendMany()
	got = ear.said()
	ear.sent.emit()
	ear.sent.emit(1, 2, 3, 4, 5, 6)
	row("emit: nothing, and six values", got, ear.said())
	node.sent.disconnect(ear.hear)


func _other_signals() -> void:
	node.renamed.connect(ear.hear)
	node.sendRenamed()
	var got := ear.said()
	node.renamed.emit()
	row("emit: one of Godot's own signals", got, ear.said())
	node.renamed.disconnect(ear.hear)
	other.connect("hit", ear.hear)
	node.sendThrough(other, 9)
	got = ear.said()
	other.emit_signal("hit", 9, Vector2(9, 9))
	row("emit: another object's signal", got, ear.said())
	node.struck.connect(ear.hear)
	node.sendEach(other)
	got = ear.said()
	node.struck.emit(1, Vector2(1, 1))
	other.emit_signal("hit", 2, Vector2(2, 1))
	node.struck.emit(3, Vector2(3, 1))
	Signal().emit(4, Vector2(4, 1))
	row("emit: one place, whichever signal it is handed, and a signal of no object", got, ear.said())
	node.struck.disconnect(ear.hear)
	other.disconnect("hit", ear.hear)


## What Godot's emit hands a listener written in AS3, and what it refuses to hand one.
func _heard() -> void:
	node.listen("struck")
	node.struck.emit(5, Vector2(1, 2))
	node.struck.emit(2.0, Vector2())
	node.struck.emit(5, null)
	row("a listener's int and Vector2", node.said(), "5 %s;2 %s;5 null" % [Vector2(1, 2), Vector2()])
	node.struck.emit("five", Vector2())
	node.struck.emit(5, Vector3())
	node.struck.emit(2.5, Vector2())
	node.struck.emit(5000000000, Vector2())
	node.struck.emit(5, node)
	node.struck.emit(5)
	node.struck.emit(5, Vector2(), 1)
	row("a listener is not given what does not fit", node.said(), "")
	node.forget()
	node.listen("wide")
	node.sent.emit(7, 3, false, 5000000000)
	node.sent.emit(4000000000, 0.25, true, -1.0)
	row("a listener's uint, Number, Boolean and Int64", node.said(), "7 3 false 5000000000;4000000000 0.25 true -1")
	node.sent.emit(-1, 1.0, false, 0)
	node.sent.emit(1, "x", false, 0)
	node.sent.emit(1, 1.0, 1, 0)
	node.sent.emit(1, 1.0, true, 2.5)
	row("such a listener is not given what does not fit", node.said(), "")
	node.forget()
	node.listen("held")
	node.sent.emit("text", other, 2.5, Vector2(1, 1))
	node.sent.emit(null, null, null, null)
	row("a listener's String, node, untyped value and Vector2", node.said(), "text Other 2.5 %s;null no node null null" % Vector2(1, 1))
	node.forget()


func _heard_otherwise() -> void:
	for how in ["default", "rest", "static", "closure"]:
		node.listen(how)
	node.struck.emit(6, Vector2(7, 8))
	row("a listener with a default, with the rest, a static one and a function", node.said(), "default 6 %s;rest 6 1;closure 6 7;static 6 8" % Vector2(7, 8))
	node.struck.emit(6)
	row("the same, given less", node.said(), "default 6 null;rest 6 0")
	node.forget()
	node.listen("changed")
	node.listen("struck")
	node.listen("given")
	node.struck.emit(1, Vector2(1, 2))
	row("a listener that changes what it was given changes its own", node.said(), "changed 8 %s;1 %s" % [Vector2(100, 2), Vector2(1, 2)])
	node.forget()
	node.listen("keep")
	node.struck.emit(1, Vector2(1, 2))
	node.struck.emit(2, Vector2(3, 4))
	row("a listener that keeps what it was given keeps its own", node.kept(), "%s %s/%s %s" % [Vector2(3, 4), Vector2(1, 2), Vector2(3, 4), Vector2(3, 4)])
	node.forget()
	# The Vector2 fits and the String after it does not: nothing of the call is made.
	node.listen("turned")
	node.struck.emit(Vector2(1, 2), 5)
	node.struck.emit(Vector2(1, 2), "five")
	row("a listener is not given what fits only in part", node.said(), "turned %s 5" % Vector2(1, 2))
	node.forget()


## AS3's emit to a listener written in AS3, with nothing of GDScript's between.
func _own() -> void:
	node.listen("struck")
	node.strike(4)
	node.strikes(3)
	row("AS3 emits to AS3", node.said(), "4 %s;0 %s;1 %s;2 %s" % [Vector2(4, 1), Vector2(0, 1), Vector2(1, 1), Vector2(2, 1)])
	node.strikeWrong()
	node.strikeShort()
	row("AS3 emits what its own listener does not take", node.said(), "")
	node.listen("fail")
	row("a listener's error comes out of the emit", [node.catches(2), node.said()], ["caught in the listener at 2", "2 %s;failing 2" % Vector2(2, 1)])
	node.strike(3)
	row("a listener's error nothing catches ends the emitting function", node.said(), "3 %s;failing 3" % Vector2(3, 1))
	node.forget()
	node.listen("again")
	node.ring(3)
	row("a listener that emits what it listens to", node.said(), "3@1;2@2;1@4;0@8")
	node.forget()
	node.listen("once")
	node.strike(1)
	node.strike(2)
	row("a listener for once only", node.said(), "1 %s" % Vector2(1, 1))
	node.forget()


func _lifetimes() -> void:
	# A signal of an object that has been freed since.
	var gone := Node.new()
	gone.add_user_signal("hit")
	root.add_child(gone)
	gone.connect("hit", ear.hear)
	node.keep(gone)
	var kept := Signal(gone, "hit")
	var before: String = node.sendKept(1) + " " + ear.said()
	kept.emit(1, Vector2(1, 0))
	row("emit: a signal kept in a field", before, "sent " + ear.said())
	gone.free()
	# Nothing happens, and nothing is said, as when GDScript emits the same signal.
	var after: String = node.sendKept(2) + " " + ear.said()
	kept.emit(2, Vector2(2, 0))
	row("emit: a signal of an object freed since", after, "sent " + ear.said())
	node.keepNone()
	row("emit: no signal at all", node.sendKept(3), "caught Error #1009: Cannot access a property or method of a null object reference.")
	# A listener's object freed by the listener before it, in the same emit.
	var second := fresh("Second")
	node.victim = second
	node.listen("free")
	second.listenTo(node)
	node.strike(1)
	var twin := Ear.new()
	var twin_victim := Ear.new()
	root.add_child(twin)
	root.add_child(twin_victim)
	twin.victim = twin_victim
	twin.struck.connect(twin.free_it)
	twin.struck.connect(twin_victim.hear)
	twin.struck.emit(1, Vector2(1, 1))
	row("a listener freed by the one before it is not called", [node.said(), is_instance_valid(second)], [twin.said(), is_instance_valid(twin_victim)])
	twin.queue_free()
	node.forget()
	# A listener that puts another signal in the field the emit was made through.
	node.sent.connect(ear.hear)
	node.swaps()
	var swapped := [node.sendKept(1), node.said(), node.sendKept(2), node.said(), ear.said()]
	ear.hear(2, Vector2(2, 0))
	row("a listener that changes the field the signal was emitted through", swapped, ["sent", "swapped 1", "sent", "", ear.said()])
	node.sent.disconnect(ear.hear)
	node.forget()
	# Left for finish(): a listener called later, at the end of the frame.
	node.listen("later")
	node.strike(4)
	row("a listener called later has heard nothing yet", node.said(), "")
