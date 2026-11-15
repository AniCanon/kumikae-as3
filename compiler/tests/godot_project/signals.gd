# Signals in AS3. Each line shows what happened and what should have; run_godot.py checks they agree.
extends SceneTree

var node: Node
var other: Node
var late: Node
var looped: WeakRef
var looped_far: WeakRef
var looped_kept: WeakRef
var kept_listener: Callable
var removed_kept: WeakRef
var frames := 0

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func fresh() -> Node:
	var made := Node.new()
	made.set_script(load("res://Signals.as"))
	root.add_child(made)
	return made

func rename(what: Node) -> void:
	what.name = what.name + "x"

func _initialize() -> void:
	node = fresh()
	other = Node.new()
	root.add_child(other)

func _physics_process(_delta: float) -> bool:
	frames += 1
	if frames < 2:
		return false
	if frames == 3:
		# The end of the frame has passed: the deferred listener has run, once.
		show("deferred after the frame", node.count, 1)
		# Its owner was freed before the frame ended: nothing ran, nothing crashed.
		show("deferred with its owner freed", is_instance_valid(late), false)
		return false
	if frames > 3 and frames < 90:
		return false # Whole frames pass: listeners are looked over once in each.
	if frames > 90 and frames < 180:
		return false
	if frames == 180:
		# Once nothing holds the listener's callable either, that loop goes too.
		show("a loop goes when its callable is let go", looped_kept.get_ref() == null, true)
		node.queue_free()
		other.queue_free()
		return true
	if frames == 90:
		# A listener and the object it listens to, held only by each other, are both gone.
		show("a loop is freed", looped.get_ref() == null, true)
		show("a longer loop is freed", looped_far.get_ref() == null, true)
		show("a removed listener that is still held keeps what it holds", removed_kept.get_ref() != null, true)
		# While GDScript holds the listener's callable, the listener stays where it was.
		var still = looped_kept.get_ref()
		show("a loop whose callable is held stays", [still != null, still.changed.get_connections().size() if still != null else -1, kept_listener.is_valid()], [true, 1, true])
		still = null
		kept_listener = Callable()
		# A listener that only its signal holds is still there, and still hears.
		var before: int = node.total()
		rename(other)
		show("an unheld listener stays", node.total() - before, 1)
		return false
	if frames > 180:
		return true
	# add: the listener hears the signal.
	node.listen()
	rename(node)
	show("add", node.count, 1)
	show("has", node.isListening(), true)
	# A second add of the same listener does nothing, written either way.
	node.listenTwice()
	show("one listener", [node.listeners(), node.renamed.get_connections().size()], [1, 1])
	rename(node)
	show("still once", node.count, 2)
	# emit from AS3.
	node.fire()
	show("emit", node.count, 3)
	# remove.
	node.stop()
	rename(node)
	show("remove", [node.count, node.isListening(), node.listeners()], [3, false, 0])
	node.stop() # Removing a listener that is not there does nothing.
	# addOnce.
	node.listenOnce()
	rename(node)
	rename(node)
	show("add once", node.count, 103)
	# removeAll.
	node.listen()
	node.listenOnce()
	node.stopAll()
	rename(node)
	show("remove all", [node.count, node.listeners()], [103, 0])
	# removeAll takes away what AS3 added, and leaves listeners that GDScript or the engine itself added.
	var theirs := func(): pass
	node.renamed.connect(theirs)
	node.listen()
	show("counts what scripts added", [node.listeners(), node.renamed.get_connections().size()], [1, 2])
	node.stopAll()
	show("remove all leaves others", [node.isListening(), node.renamed.is_connected(theirs), node.listeners()], [false, true, 0])
	# The same listener can't be both lasting and once-only: the second call is an error, as in as3-signals.
	node.mixed()
	node.mixedBack()
	show("lasting or once, not both", node.listeners(), 2)
	node.stopAll()
	# A signal made from nothing; null as an argument; arguments are worked out before a wrong name fails.
	node.empty()
	other.add_user_signal("ping", [{"name": "n", "type": TYPE_OBJECT}])
	node.reset()
	node.ping(other)
	node.wrongName(other)
	show("null argument and order", node.count, 1050)
	node.renamed.disconnect(theirs)
	# Arguments reach the listener.
	node.reset()
	node.listenChild()
	var kid := Node.new()
	kid.name = "kid"
	node.add_child(kid)
	show("arguments", [node.count, node.last], [10, "kid"])
	# A listener that removes itself while the signal is being sent.
	node.reset()
	node.listenSelfRemoving()
	rename(node)
	rename(node)
	show("removes itself", [node.count, node.listeners()], [1, 0])
	# An error inside a listener is reported, and the next listener still runs.
	node.reset()
	node.listenFailing()
	rename(node)
	show("after a failing listener", node.count, 1)
	node.stopAll()
	# A listener on another object's signal; a plain AS3 object as the listener.
	node.reset()
	node.listenTo(other)
	node.plain(other)
	rename(other)
	show("another object's signal", [node.count, node.plainCount()], [1, 1])
	other.renamed.get_connections().map(func(c): other.renamed.disconnect(c.callable))
	# By a name known only as the program runs: the AS3 name.
	node.reset()
	node.listenByName(other, "renamed")
	node.listenByName(node, "treeExiting")
	rename(other)
	show("by name", [node.count, node.tree_exiting.get_connections().size()], [1, 1])
	node.listenByName(other, "nothingAtAll")
	node.listenByName(other, "name")
	show("by a wrong name", other.renamed.get_connections().size(), 1)
	node.reset()
	node.emitByName(other, "renamed")
	show("listeners and emit by name", [node.listenersByName(other, "renamed"), node.count], [1, 1])
	# A signal that GDScript defines, with an argument that is checked.
	other.add_user_signal("hit", [{"name": "n", "type": TYPE_INT}])
	node.reset()
	node.userSignal(other)
	other.emit_signal("hit", 5)
	node.fireUser(other, 2)
	other.emit_signal("hit", "five")
	other.emit_signal("hit", 1, 2)
	show("user signal", node.count, 7)
	# A signal the script declares: Godot knows it, GDScript and AS3 both listen, AS3 emits it.
	node.reset()
	var heard := []
	node.scored.connect(func(n): heard.append(n))
	node.listenOwn()
	node.score(4)
	node.emit_signal("scored", 3)
	show("declared signal", [node.has_signal("scored"), node.ownSignalName(), heard, node.count], [true, "scored", [4, 3], 7])
	show("declared signal listed", node.get_signal_list().any(func(s): return s.name == "scored"), true)
	node.stopAll()
	# A signal kept in a script's own field, reached by the field's name.
	node.stopAll()
	node.reset()
	node.byOwnField("mine")
	rename(node)
	node.byOwnField("count")
	show("own signal field by name", node.count, 1)
	node.stopAll()
	# A method as a function anywhere Godot takes one.
	show("filter", node.filtered([1, 2, 3, 4]), [3, 4])
	# The listener goes when its owner goes: nothing is left connected, and a kept callable fails cleanly.
	var gone := fresh()
	gone.listenTo(other)
	var before := other.renamed.get_connections().size()
	var kept: Callable = other.renamed.get_connections()[before - 1].callable
	show("valid while its owner lives", kept.is_valid(), true)
	gone.free()
	show("gone with its owner", [other.renamed.get_connections().size(), kept.is_valid()], [before - 1, false])
	kept.callv([]) # Refused: the object it belonged to is gone.
	rename(other)
	# Deferred: runs at the end of the frame, not now.
	node.stopAll()
	other.renamed.get_connections().map(func(c): other.renamed.disconnect(c.callable))
	node.reset()
	node.listenLater(other)
	late = fresh()
	late.listenLater(other)
	rename(other)
	show("deferred not yet", node.count, 0)
	var resource := Resource.new()
	looped = weakref(resource)
	node.loop(resource)
	resource = null
	show("a loop lives until the frame ends", looped.get_ref() != null, true)
	resource = Resource.new()
	looped_far = weakref(resource)
	node.loopFar(resource)
	resource = Resource.new()
	looped_kept = weakref(resource)
	node.loop(resource)
	kept_listener = resource.changed.get_connections()[0].callable
	resource = Resource.new()
	removed_kept = weakref(resource)
	node.removedButKept(resource)
	resource = null
	other.renamed.get_connections().map(func(c): other.renamed.disconnect(c.callable))
	node.unheld(other)
	# Still held when Godot shuts down, with a GDScript listener on it too: nothing may crash then.
	var at_exit := Resource.new()
	at_exit.changed.connect(func(): pass)
	node.keepPlain(at_exit)
	at_exit = null
	late.free()
	return false
