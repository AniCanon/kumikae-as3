# The editor's first scan of a project it has never opened (run with --editor, in a copy of the project).
# Godot crashes if it is told to quit while its documentation thread for the new classes still runs, so
# this ends when the work has: the scan of the files over, and the documentation asked for, which waits
# for that thread. Nothing here counts frames or looks at the clock.
extends SceneTree

func _process(_delta: float) -> bool:
	var files := EditorInterface.get_resource_filesystem()
	if files == null or files.is_scanning() or not FileAccess.file_exists("res://.godot/extension_list.cfg"):
		return false # The scan is over once the project's extensions are listed.
	# Opening a page of the documentation waits for the thread that makes it (EditorHelp's doc data).
	EditorInterface.get_script_editor().goto_help("class_name:Node")
	print("SCAN done")
	return true
