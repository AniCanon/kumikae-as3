package {
	import godot.*;

	// Code that runs as the scene tree is taken down at exit (exit_tree.gd): its listeners are still called,
	// and static fields still hold their values.
	public class Late extends Node {
		public static var word:String = "unset";

		override public function _ready():void { word = "kept"; treeExited.add(onExited); }
		private function onExited():void { trace("LATE static=" + word); }
	}
}
