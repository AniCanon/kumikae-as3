# What signals carry between AS3 and Godot (Emits.as). Each line shows what happened and what should have;
# run_godot.py checks they agree.
extends SceneTree

var cases: RefCounted
var frames := 0


func _initialize() -> void:
	cases = load("res://emits_cases.gd").new()
	cases.begin(root)


func _process(_delta: float) -> bool:
	frames += 1
	if frames < 3:
		return false
	if frames == 3:
		cases.finish()
		for row in cases.rows:
			print("VALUE %s=%s|%s" % [row[0], str(row[1]), str(row[2])])
		return false
	return frames > 5
