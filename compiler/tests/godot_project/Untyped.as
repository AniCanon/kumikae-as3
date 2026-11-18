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
		// The text of a node whose script's toString() throws: the error comes out here, where it is caught.
		public function caughtText(value:*):String {
			try { return "" + value; } catch (e:Error) { return "caught " + e.message; }
			return "not caught";
		}
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
		// The script's own object held untyped is the Godot object it is attached to, as it is held typed.
		public function selfHeld():Array {
			var held:* = this;
			var object:Object = this;
			var stored:Node = held;
			var plain:* = [1];
			return [held is Node, held is Node2D, held is Sprite2D, (held as Node) != null, String((held as Node).name) == String(name), object is Node,
					(object as Node2D) != null, (object as Sprite2D) == null, stored != null, String(Node2D(held).name) == String(name), plain is Node, (plain as Node) == null];
		}
		public function minusOne(value:*):* { return value - 1; }
		public function twice(value:*):* { return value * 2; }
		public function stepped(value:*):* { value++; return value; }
		public function half(value:*):* { return value / 2; }
		public function plusPosition(by:*):* { return position + by; }
		public function positionAfter(by:*):* { return by + position; }
		public function addPosition(x:*):* { x += position; return x; }
		// The other operators on untyped values that hold Godot's: Godot's own, as typed code has them.
		public function times(a:*, b:*):* { return a * b; }
		public function minus(a:*, b:*):* { return a - b; }
		public function over(a:*, b:*):* { return a / b; }
		public function remainder(a:*, b:*):* { return a % b; }
		public function negated(a:*):* { return -a; }
		public function ordered(a:*, b:*):String { return "" + (a < b) + (a <= b) + (a > b) + (a >= b); }
		public function scaled(a:*, b:*):* { a *= b; a -= b; return a; }
		public function positionTimes(by:*):* { return position * by; }
		public function timesText(a:*, b:*):String { return "" + a * b + " " + (a - b) + " " + -a; }
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
		// JSON has no form for one of Godot's values or objects: stringify() refuses one wherever it is, the
		// interpreter's ruling, so that nothing is written as a text that can't be read back.
		public function json(value:*):String {
			try {
				return JSON.stringify({p: [1, value]});
			} catch (e:TypeError) {
				return "refused: " + e.message;
			}
			return "never";
		}
		// `is`, `as` and a cast with one of Godot's value types: what the value is now decides.
		public function isValue(value:*):Array {
			return [value is Vector2, value is Color, value is Vector2i, value is GodotArray, value is PackedInt32Array];
		}
		public function asValue(value:*):String {
			var point:Vector2 = value as Vector2;
			var held:Object = value;
			return point + "/" + (held as Color) + "/" + (value as GodotDictionary);
		}
		public function castValue(value:*):Vector2 {
			return Vector2(value);
		}
		public function typedValue(point:Vector2, tint:Color):String {
			var none:Vector2 = null;
			var plain:Thing = new Thing();
			return (point is Vector2) + "/" + (point is Color) + "/" + (tint as Color) + "/" + (tint as Vector2) + "/" + (none is Vector2) + "/" + (none as Vector2) +
					"/" + (5 is Vector2) + "/" + (plain is Vector2) + "/" + Vector2(point) + "/" + Vector2(none) + "/" + (this is Vector2);
		}
		// A Godot object freed while an untyped value holds it is null, as a typed one is.
		public function freed():String {
			var n:Node = new Node();
			var x:* = n;
			var o:Object = n;
			n.free();
			// Stored where a Node is declared, it is null there too.
			var back:Node = x;
			return "" + (n == null) + (x == null) + (o == null) + (x ? "yes" : "no") + (o === null) + (back == null);
		}
	}
}

class Thing {
}
