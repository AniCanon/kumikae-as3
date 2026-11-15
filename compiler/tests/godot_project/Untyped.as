package {
	import godot.*;

	// Untyped values between AS3 and Godot: untyped.gd does the same in GDScript.
	public class Untyped extends Node2D {
		public function describe(value:*):String { return typeof value + ":" + value; }
		public function same(value:*):* { return value; }
		public function copyMeta(target:Node, from:String, to:String):void { target.setMeta(to, target.getMeta(from)); }
		public function point(table:GodotDictionary):Vector2 {
			var at:Vector2 = table["position"];
			return at;
		}
		public function count(table:GodotDictionary):int {
			var many:int = table["count"];
			return many + 1;
		}
		public function wrongType(table:GodotDictionary):Vector2 {
			var at:Vector2 = table["count"];
			return at;
		}
		public function scriptObject(target:Node):void {
			var mine:* = new Thing();
			target.setMeta("mine", mine);
		}
		public function equal(a:*, b:*):String { return "" + (a == b) + (a === b); }
		public function order(a:*, b:*):String { return "" + (a == b) + (a === b) + (a < b) + (a <= b); }
		// A store into one of Godot's types checks the value is of that type.
		public function asVector(value:*):Vector2 { var v:Vector2 = value; return v; }
		public function asNode(value:*):Node { var n:Node = value; return n; }
		public function place(value:*):Vector2 { position = value; return position; }
		// A script object does not reach Godot as a null with nothing said.
		public var keep:* = new Thing();
		public function giveScript():* { return new Thing(); }
		// A Godot object freed while an untyped value holds it is null, as a typed one is.
		public function freed():String {
			var n:Node = new Node();
			var x:* = n;
			var o:Object = n;
			n.free();
			return "" + (n == null) + (x == null) + (o == null) + (x ? "yes" : "no") + (o === null);
		}
	}
}

class Thing {
}
