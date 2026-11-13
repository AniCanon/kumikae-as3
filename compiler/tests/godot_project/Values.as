package {
	import godot.*;

	// Godot's values in AS3: each method is called by values.gd, which works the same thing out in GDScript.
	public class Values extends Node2D {
		public var calls:int = 0;

		public function add(a:Vector2, b:Vector2):Vector2 { return a + b; }
		public function sub(a:Vector2, b:Vector2):Vector2 { return a - b; }
		public function times(a:Vector2, n:Number):Vector2 { return a * n; }
		public function scaleInt(a:Vector2, n:int):Vector2 { return a * n; }
		public function scaleLeft(n:Number, a:Vector2):Vector2 { return n * a; }
		public function divide(a:Vector2, n:Number):Vector2 { return a / n; }
		public function negate(a:Vector2):Vector2 { return -a; }
		public function cells(a:Vector2i, n:int):Vector2i { return a * n; }
		public function cellsNumber(a:Vector2i, n:Number):Vector2 { return a * n; }
		public function cellsOver(a:Vector2i, n:int):Vector2i { return a / n; }
		public function tint(a:Color, b:Color):Color { return a * b; }
		public function same(a:Vector2, b:Vector2):Boolean { return a == b; }
		public function differs(a:Vector2, b:Vector2):Boolean { return a != b; }
		public function sameNode(a:Node, b:Node):Boolean { return a == b; }
		public function otherNode(a:Node, b:Node):Boolean { return a != b; }
		public function chain(a:Vector2, b:Vector2):Vector2 { return (a + b) * 2 - a / 2; }
		public function compound(a:Vector2, b:Vector2):Vector2 {
			var v:Vector2 = a;
			v += b;
			v *= 3;
			return v;
		}
		public function moved(by:Vector2):Vector2 { return position + by; }

		// Text.
		private var kept:Node;
		public function text(v:Vector2):String { return "at " + v; }
		public function textAfter(v:Vector2):String { return v + " here"; }
		public function cast(v:Vector2):String { return String(v); }
		public function own(v:Vector2):String { return v.toString(); }
		public function colorText(c:Color):String { return String(c); }
		public function nodeText(n:Node):String { return n.toString(); }
		public function nodeJoined(n:Node):String { return "node " + n; }
		public function nullText():String { var v:Vector2; return "v=" + v; }
		public function nullNode(n:Node):String { return String(n); }
		public function nullOwn(n:Node):String { return n.toString(); }
		public function traced(v:Vector2):void { trace(v, v.x); }
		public function keep(n:Node):void { kept = n; }
		public function keptText():String { return "kept " + kept; }
	}
}
