# Not a test: writes the project into one pack file, as an exported game has it. See run_godot.py.
#   godot --headless --path . -s pack.gd -- <out.pck>
extends SceneTree
func _initialize() -> void:
	var packer := PCKPacker.new()
	print("start ", packer.pck_start(OS.get_cmdline_user_args()[0]))
	var names := ["project.godot", "kas3.gdextension", ".godot/extension_list.cfg"]
	var dir := DirAccess.open("res://")
	for name in dir.get_files():
		if name.ends_with(".as") or name.ends_with(".gd"):
			names.append(name)
	for sub in ["a", "b"]:
		for name in DirAccess.open("res://" + sub).get_files():
			if name.ends_with(".as"):
				names.append(sub + "/" + name)
	for name in names:
		packer.add_file("res://" + name, ProjectSettings.globalize_path("res://" + name))
	print("flush ", packer.flush(), " ", names.size(), " files")
	quit()
