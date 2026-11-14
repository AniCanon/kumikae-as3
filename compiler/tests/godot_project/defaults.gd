# Not a scene: asks Godot to read every default the name table records for a parameter AS3 can't write
# a default for. See run_godot.py.
#   godot --headless --path . -s defaults.gd -- <godot_names.txt>
extends SceneTree

func _initialize() -> void:
	var failed := []
	var seen := {}
	var file := FileAccess.open(OS.get_cmdline_user_args()[0], FileAccess.READ)
	while not file.eof_reached():
		var line := file.get_line()
		if not line.begins_with("@default."):
			continue
		var text := line.substr(line.find("=") + 1)
		if seen.has(text):
			continue
		seen[text] = true
		var expression := Expression.new()
		var value = expression.execute([], null, false) if expression.parse(text) == OK else null
		if value == null or expression.has_execute_failed():
			failed.append(text)
	print("DEFAULTS read=%d failed=%s" % [seen.size(), str(failed)])
	quit()
