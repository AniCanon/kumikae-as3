extends "res://matrix.gd"

var order := ""
var factor := 1.0

func batch(row: String, _target: Variant, rounds: int, _mode: String, _targets: Array, _script: Node, _cs: Node) -> int:
	order += "A" if row == "timer/read" else "B"
	var cost := 21.0 if row == "timer/read" else 20.0
	return roundi(cost * rounds / 20000 * factor)

func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	var rows := ["timer/read", "timerTyped/read"]
	var sizes := {rows[0]: int(args[0]), rows[1]: int(args[1])}
	for layout in [1.1, 0.9]:
		factor = layout
		order = ""
		var samples := sample_window(rows.duplicate(), sizes, false, {"timer": null, "timerTyped": null}, "", [], null, null)
		print("ORDER ", order)
		for row: String in rows:
			print("SAMPLE ", row, " ", median(samples[row]))
	quit()
