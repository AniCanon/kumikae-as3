package {
	import godot.*;

	// What the review of the first Godot slice found, each as a method the driver calls.
	public class Checks extends Node {
		public var readied:int = 0;
		private var child:Node;

		override public function _ready():void {
			readied++;
			super._ready();
		}

		public function keep():void { child = getChild(0); }
		public function countOfKept():int { return child.getChildCount(); }

		public function two(a:Number, b:Number):Number { return a * 10 + b; }
		public function withDefault(a:int, b:int = 7):int { return a + b; }
		public function takeInt(n:int):int { return n; }
		public function takeUint(n:uint):Number { return n; }
		public function takeNumber(n:Number):Number { return n; }
		public function takeString(s:String):String { return s; }
		public function takeNode(n:Node):Boolean { return n != null; }
		public function takeFlag(b:Boolean):Boolean { return b; }
	}
}
