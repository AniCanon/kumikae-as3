package {
	import godot.*;

	// A script that says how it reads: Godot shows the same text.
	public class Named extends Node {
		public var title:String = "the one";
		public function toString():String { return "Named(" + title + ")"; }
		public function describe():String { return "I am " + this; }
	}
}
