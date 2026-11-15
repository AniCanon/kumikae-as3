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
		public function throwValue(value:*):void { throw value; }
		public function increment(value:*):* { return value + 1; }
		public function moveBy(by:*):Vector2 { var q:Vector2 = position + by; return q; }
		// The object written is the one named before the value changes the local: AS3's order.
		public function writeOrder(a:Node2D, b:Node2D):Vector2 {
			var first:Node2D = a;
			a.position.x = (a = b, 5);
			first.visible = true;
			b.visible = true;
			a = first;
			a.visible = (a = b, false);
			return new Vector2(first.position.x, first.visible ? 1 : 0);
		}
		// A Godot value read above the line that gives it a value is its zero, not null.
		public var early:String = seen();
		public var later:Vector2 = Vector2.ONE;
		public static var seenStatic:String = seenStatics();
		public static var laterStatic:Vector2 = Vector2.ONE;
		private function seen():String { return "" + later; }
		private static function seenStatics():String { return "" + laterStatic; }
		public function readEarly():String {
			var before:String = "" + v;
			var v:Vector2 = Vector2.ONE;
			return before + " " + early + " " + seenStatic + " " + origin;
		}
		// `is` and `as` with one of Godot's classes, on an untyped value.
		public function holdsNode(value:*):Boolean { return value is Node; }
		public function holdsNode2D(value:*):Boolean { return value is Node2D; }
		public function asNodeOrNull(value:*):Node { return value as Node; }
		public function minusOne(value:*):* { return value - 1; }
		public function twice(value:*):* { return value * 2; }
		public function stepped(value:*):* { value++; return value; }
		public function half(value:*):* { return value / 2; }
		public function plusPosition(by:*):* { return position + by; }
		public function positionAfter(by:*):* { return by + position; }
		public function addPosition(x:*):* { x += position; return x; }
		// An error raised for a Godot value is caught like any other.
		public function guarded(value:*):String {
			try {
				var v:Vector2 = value;
				return "fits " + v;
			} catch (e:TypeError) {
				return "caught " + (e is TypeError);
			} finally {
				position = new Vector2(1, 1);
			}
			return "never";
		}
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
