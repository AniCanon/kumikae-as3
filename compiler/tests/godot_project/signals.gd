# Signals in AS3. Each line shows what happened and what should have; run_godot.py checks they agree.
extends SceneTree

var node: Node
var other: Node
var late: Node
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
		node.queue_free()
		other.queue_free()
		return true
	if frames > 3:
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
	# A signal that GDScript defines, with an argument that is checked.
	other.add_user_signal("hit", [{"name": "n", "type": TYPE_INT}])
	node.reset()
	node.userSignal(other)
	other.emit_signal("hit", 5)
	node.fireUser(other, 2)
	other.emit_signal("hit", "five")
	show("user signal", node.count, 7)
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
	node.keepPlain(Resource.new()) # Still held when Godot shuts down: nothing may crash then.
	late.free()
	return false
