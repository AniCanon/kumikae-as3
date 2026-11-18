extends Node
## Runs one of the frame benchmarks in one language and prints one JSON line: main.gd makes one of these when
## the program is given `--bench`. The benchmark's script is put on a Node2D and left to Godot's frames; what
## is timed is each whole frame, after a warm-up.
## Usage: <program> --headless --fixed-fps 60 -- --bench=NAME --lang=gd|cs|as [--mode=verify|measure]
## [--frames=N] [--warmup=N] [--verify-frames=N]
## With --verify-frames a verify run ends after exactly that many frames, whatever the clock says: two such
## runs of different lengths give what a frame costs in anything counted for the whole process.
## Both runs must be long enough that .NET has finished optimising the C# script (some thousands of frames
## of a benchmark that takes a tenth of a millisecond): its first few hundred frames run code it has not
## optimised yet, and count two or three times the instructions its later frames take.

const BENCHES: PackedStringArray = ["empty", "move_nodes", "per_node_process", "ray_queries", "signal_traffic",
		"nbody", "sieve", "array_conversion", "saxpy", "shapes", "alloc"]
const LANGS: PackedStringArray = ["gd", "cs", "as"]
var VERIFY_FRAMES := 10
# Warm-up ends only when BOTH are met. Frames alone are not enough: a benchmark that runs in 0.1 ms would
# finish 300 frames before .NET has finished optimising its code.
var WARMUP_FRAMES := 300
const WARMUP_USEC := 2000000
var MEASURE_FRAMES := 600

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
var _start_usec := 0
var _warming := true
var _warmup_frames := 0
var _warmup_usec := 0


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--bench="):
			_bench_name = arg.trim_prefix("--bench=")
		elif arg.begins_with("--lang="):
			_lang = arg.trim_prefix("--lang=")
		elif arg.begins_with("--mode="):
			_mode = arg.trim_prefix("--mode=")
		elif arg.begins_with("--frames="):
			MEASURE_FRAMES = int(arg.trim_prefix("--frames="))
		elif arg.begins_with("--warmup="):
			WARMUP_FRAMES = int(arg.trim_prefix("--warmup="))
		elif arg.begins_with("--verify-frames="):
			VERIFY_FRAMES = int(arg.trim_prefix("--verify-frames="))
	if _bench_name == "":
		_fail("usage: -- --bench=NAME --lang=gd|cs|as --mode=verify|measure")
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
	var script := _load_script()
	if script == null:
		_fail("cannot load bench: %s/%s" % [_lang, _bench_name])
		return
	_bench = Node2D.new()
	_bench.name = "Bench"
	_bench.set_script(script)
	add_child(_bench)


func _class_name() -> String:
	return "NBody" if _bench_name == "nbody" else _bench_name.to_pascal_case()


func _load_script() -> Script:
	match _lang:
		"gd":
			return load("res://frames/%s.gd" % _bench_name)
		"cs":
			return load("res://frames/%s.cs" % _class_name())
		_:
			return load("res://frames/%s.as" % _class_name())


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
	if _warming:
		if _start_usec == 0:
			_start_usec = now
		if _frames <= _slow_warmup:
			OS.delay_msec(20)
		if _frames >= WARMUP_FRAMES and now - _start_usec >= WARMUP_USEC:
			_warming = false
			_warmup_frames = _frames
			_warmup_usec = now - _start_usec
		_last_usec = Time.get_ticks_usec()
		return
	_times.append(now - _last_usec)
	_last_usec = now
	if _times.size() == MEASURE_FRAMES:
		_times.sort()
		_finish({
			"frames": MEASURE_FRAMES,
			"warmup_frames": _warmup_frames,
			"warmup_ms": _warmup_usec / 1000,
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
