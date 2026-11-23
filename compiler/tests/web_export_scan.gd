extends SceneTree

# Godot exports from its file-scan callback and quits immediately. Join the editor's
# documentation worker in this same process before the export can take down EditorNode.
var ready := false

func _process(_delta: float) -> bool:
	if ready:
		return false
	ready = true
	EditorInterface.get_script_editor().goto_help("class_name:Node")
	print("Web export documentation ready")
	return false
