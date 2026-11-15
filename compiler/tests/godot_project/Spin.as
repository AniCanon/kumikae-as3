package {
	import godot.*;

	// A script stuck in a loop: timeout.gd checks it is stopped with Flash's error, and the game goes on.
	public class Spin extends Node {
		public var turns:int;
		public function forever():int { while (true) { turns++; } return 0; }
		public function caught():String { try { for (;;) {} } catch (e:*) { return "caught"; } return "out"; }
		public function quick():int { return 7; }
	}
}
