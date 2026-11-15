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
	}
}

class Thing {
}
