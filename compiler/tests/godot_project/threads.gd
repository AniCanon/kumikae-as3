# Godot reaching a script from its worker threads: AS3 runs on the main thread only, so what would run AS3
# there is refused and said so, and what must happen there (a free) is done on the main thread.
extends SceneTree

func show(name: String, got, want) -> void:
	print("VALUE %s=%s|%s" % [name, str(got), str(want)])

func _initialize() -> void:
	var script = load("res://Threaded.as")
	var on_main := RefCounted.new()
	on_main.set_script(script)
	show("toString on the main thread", str(on_main), "threaded")
	var seen := []
	var task := WorkerThreadPool.add_task(func(): seen.append(str(on_main)))
	WorkerThreadPool.wait_for_task_completion(task)
	show("toString from a worker is refused: Godot's own text", [seen.size(), str(seen[0]).begins_with("<RefCounted#")], [1, true])

	var before: int = on_main.call("madeCount")
	var attached := []
	task = WorkerThreadPool.add_task(func():
		var o := RefCounted.new()
		o.set_script(script)
		attached.append(o.get_script() == null))
	WorkerThreadPool.wait_for_task_completion(task)
	show("attached on a worker is refused: no script, no constructor run", [attached, on_main.call("madeCount") - before], [[true], 0])

	# Its lists of properties and methods asked for on workers and on the main thread at once.
	var listed := RefCounted.new()
	listed.set_script(script)
	var group := WorkerThreadPool.add_group_task(func(_i):
		for k in 2000:
			listed.get_property_list()
			listed.get_method_list(), 4)
	var counts := []
	for k in 2000:
		counts = [listed.get_property_list().size() > 0, listed.get_method_list().size() > 0]
	WorkerThreadPool.wait_for_group_task_completion(group)
	show("lists asked for on workers and on the main thread at once", counts, [true, true])

	# Script resources loaded on workers at once, as a scene loaded in a thread loads its scripts.
	var loaded := []
	loaded.resize(4)
	var loads := WorkerThreadPool.add_group_task(func(i):
		var fine := true
		for k in 300:
			var again = ResourceLoader.load("res://Threaded.as", "", ResourceLoader.CACHE_MODE_IGNORE)
			fine = fine and again != null and again.can_instantiate()
		loaded[i] = fine, 4)
	WorkerThreadPool.wait_for_group_task_completion(loads)
	show("scripts loaded on workers at once", loaded, [true, true, true, true])

	# The last reference let go on a worker: the script object goes on the main thread, at the end of a frame.
	var box := [on_main]
	var partner: int = on_main.call("partnerID")
	on_main = null
	task = WorkerThreadPool.add_task(func(): box.clear())
	WorkerThreadPool.wait_for_task_completion(task)
	show("freed on a worker: nothing of AS3's freed there", is_instance_id_valid(partner), true)
	await process_frame # The language's frame() comes after process_frame: this one's, and the next.
	await process_frame
	show("freed on a worker: freed on the main thread", is_instance_id_valid(partner), false)
	quit()
