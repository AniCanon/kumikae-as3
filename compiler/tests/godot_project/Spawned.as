package {
	import godot.*;

	// Made with `new` by Values.as: a script class that extends one of Godot's classes.
	public class Spawned extends Node2D {
		public var value:int = 1;
		public function Spawned(start:int = 3) {
			value = start * 2;
			name = "spawned";
		}
		public function doubled():int { return value * 2; }
	}
}
