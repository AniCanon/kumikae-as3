package {
	import godot.*;

	// A node whose text throws (untyped.gd): what AS3 runs inside Godot's call fails, and the error comes
	// out where the text was asked for.
	public class Thrower extends Node {
		public function toString():String { throw new Error("from toString"); }
	}
}
