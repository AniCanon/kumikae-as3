package {
	import godot.*;

	// Script objects of reference-counted classes made with `new`: kept.gd checks each lives exactly as long
	// as something holds it, AS3 or Godot, and no longer.
	public class Kept extends Node {
		public var counted:Counted;
		public var resource:Stored;

		public function make():String {
			counted = new Counted();
			counted.n = 7;
			resource = new Stored();
			resource.label = "kept";
			return counted.hello() + " " + counted.n + " " + resource.label;
		}
		public function drop():void { counted = null; resource = null; }
		public function giveCounted():Counted {
			var made:Counted = new Counted();
			made.n = 11;
			return made;
		}
		public function giveStored():Stored {
			var made:Stored = new Stored();
			made.label = "given";
			return made;
		}
		public function heldN():int { return Counted.held ? Counted.held.n : -1; }
		public function letGoOfHeld():void { Counted.held = null; }
		public function makeAndForget():Int64 { var lost:Counted = new Counted(); return lost.getInstanceID(); }
		public function selfListening():Int64 {
			var made:Stored = new Stored();
			made.listen();
			made.emitChanged();
			return made.heard == 1 ? made.getInstanceID() : 0;
		}
		public function listening(other:Node):Int64 {
			var made:Counted = new Counted();
			made.listenTo(other);
			return made.getInstanceID();
		}
	}
}
