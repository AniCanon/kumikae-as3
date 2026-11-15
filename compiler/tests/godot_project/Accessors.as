package {
	import godot.*;

	// Getters and setters, which GDScript reaches as the script's properties (accessors.gd).
	public class Accessors extends Node2D implements Sized {
		private var hp:int = 10;
		public var sets:int = 0;

		public function get health():int { return hp; }
		public function set health(p_value:int):void {
			sets++;
			hp = p_value;
		}
		public function get label():String { return "hp " + hp; }
		public function set only(p_value:Number):void { hp = int(p_value * 2); }
		public function get spot():Vector2 { return new Vector2(hp, 1); }
		public function get size():int { return hp * 10; }
		public function get broken():int { throw new Error("broken getter"); }

		// Through an interface, in a script.
		public function ownSize():int { return sizeOf(this); }
		private function sizeOf(p_sized:Sized):int { return p_sized.size; }
	}
}

interface Sized {
	function get size():int;
}
