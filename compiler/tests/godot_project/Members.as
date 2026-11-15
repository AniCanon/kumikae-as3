package {
	import godot.*;

	// What GDScript can reach of a script by name: its public fields, to read and write, and its public
	// methods. members.gd sets and reads them, and asks Godot what the script has.
	public class Members extends Node2D {
		public var count:int = 1;
		public var ratio:Number = 0.5;
		public var label:String = "a";
		public var flag:Boolean = false;
		public var spot:Vector2 = new Vector2(1, 2);
		public var items:Array = [1, 2];
		public var buddy:Node;
		public var wide:Int64;
		public var small:uint;
		public var anything:*;
		public var mine:Members;
		public const FIXED:int = 3;
		private var secret:int = 7;
		protected var guarded:int = 8;
		internal var inside:int = 9;
		var plain:int = 10;

		public function total():Number {
			var sum:Number = 0;
			for each (var item:* in items) { sum += item; }
			return sum;
		}
		public function itemCount():int { return items ? items.length : -1; }
		public function buddyName():String { return buddy ? String(buddy.name) : "none"; }
		public function hiddenValues():String { return [secret, guarded, inside, plain].join(","); }
		public function describe():String { return [count, ratio, label, flag, spot, wide, small, anything].join(" "); }
		public function twice(n:int, by:Number = 2):Number { return n * by; }
		private function secretMethod():int { return secret; }
		protected function guardedMethod():int { return guarded; }
		internal function insideMethod():int { return inside; }
		function plainMethod():int { return plain; }
		override public function _ready():void { count = count + 0; }
	}
}
