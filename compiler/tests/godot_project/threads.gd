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
