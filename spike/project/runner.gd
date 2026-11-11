extends Node
## Runs one benchmark in one language and prints one JSON line.
## Usage: godot --headless --fixed-fps 60 --path spike/project -- --bench=NAME --lang=gd|cs|as3 --mode=verify|measure

const BENCHES: PackedStringArray = ["empty", "move_nodes", "per_node_process", "ray_queries", "signal_traffic",
		"nbody", "sieve", "array_conversion"]
const LANGS: PackedStringArray = ["gd", "cs", "as3"]
const VERIFY_FRAMES := 10
const WARMUP_FRAMES := 300
const MEASURE_FRAMES := 600

var _bench_name := ""
var _lang := ""
var _mode := "verify"
var _bench: Node
var _frames := 0
var _physics_ticks := 0
var _last_delta := 0.0
var _last_usec := 0
var _times := PackedInt64Array()
var _slow_warmup := 0


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--bench="):
			_bench_name = arg.trim_prefix("--bench=")
		elif arg.begins_with("--lang="):
			_lang = arg.trim_prefix("--lang=")
		elif arg.begins_with("--mode="):
			_mode = arg.trim_prefix("--mode=")
	if _bench_name == "":
		_fail("usage: -- --bench=NAME --lang=gd|cs|as3 --mode=verify|measure")
		return
	if not BENCHES.has(_bench_name):
		_fail("unknown bench: " + _bench_name)
		return
	if not LANGS.has(_lang):
		_fail("unknown lang: " + _lang)
		return
	_slow_warmup = int(OS.get_environment("KAS3_SLOW_FIRST_FRAMES"))
	# Run after every other node each frame, so frame N of the runner sees N frames of the benchmark.
	process_priority = 1000000
	process_physics_priority = 1000000
	_bench = Node2D.new()
	_bench.name = "Bench"
	_bench.set_script(_load_script())
	add_child(_bench)


func _load_script() -> Script:
	match _lang:
		"gd":
			return load("res://benches/gd/%s.gd" % _bench_name)
		"cs":
			return load("res://benches/cs/%s.cs" % _bench_name.to_pascal_case())
		_:
			return ClassDB.class_call_static(&"SpikeScript", &"for_class", StringName(_bench_name.to_pascal_case()))


func _fail(message: String) -> void:
	set_process(false)
	set_physics_process(false)
	printerr(message)
	get_tree().quit(2)


func _physics_process(_delta: float) -> void:
	_physics_ticks += 1


func _process(delta: float) -> void:
	var now := Time.get_ticks_usec()
	_frames += 1
	_last_delta = delta
	if _mode == "verify":
		if _frames == VERIFY_FRAMES:
			_finish({"delta": _last_delta, "physics_ticks": _physics_ticks})
		return
	if _frames <= WARMUP_FRAMES:
		if _frames <= _slow_warmup:
			OS.delay_msec(20)
		_last_usec = Time.get_ticks_usec()
		return
	_times.append(now - _last_usec)
	_last_usec = now
	if _times.size() == MEASURE_FRAMES:
		_times.sort()
		_finish({
			"frames": MEASURE_FRAMES,
			"median_us": _times[MEASURE_FRAMES / 2],
			"p99_us": _times[int(MEASURE_FRAMES * 0.99)],
			"max_us": _times[MEASURE_FRAMES - 1],
		})


func _finish(fields: Dictionary) -> void:
	set_process(false)
	var method := &"Checksum" if _lang == "cs" else &"checksum"
	var row := {"bench": _bench_name, "lang": _lang}
	row.merge(fields)
	row["checksum"] = float(_bench.call(method))
	print(JSON.stringify(row))
	get_tree().quit(0)
