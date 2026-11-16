package {
	// `-`, `*`, `/`, `%` and unary `-` with an untyped operand are decided as they run (AnyOperate): with no
	// Godot value on either side they are AS3's, both operands made Numbers as avmplus's multiply and the
	// rest make them (ToNumber: text read as a number, null 0, undefined NaN, an object by its valueOf()).
	public class untyped_operators {
		public function untyped_operators() {
			var a:* = 7;
			var b:* = 2;
			trace(a * b, a - b, a / b, a % b, -a);
			var text:* = "6";
			var none:* = null;
			var missing:*;
			trace(text * "7", text - 1, text / "4", text % 4, -text);
			trace(none * 5, none - 5, -none, missing * 5, missing - 5, -missing);
			trace(true * 3, a * "x", "x" - a, a / 0, -a / 0, a % 0);
			var held:Object = new Amount(4);
			var loose:* = held;
			trace(held * 3, loose - 1, loose / 8, -loose, loose % 3);
			var n:Number = 1.5;
			var i:int = 3;
			trace(a * n, n * a, i - a, a - i, i / b, a % i);
			a *= 3;
			b -= "1";
			trace(a, b);
			a /= 2;
			a %= 4;
			n -= b;
			i *= b;
			trace(a, n, i);
			var wide:* = 2147483647;
			trace(wide * 2, wide * wide, -wide - 2, typeof (wide * 2));
			var whole:int = a * 100 - b;
			var word:String = "is " + a * 2;
			trace(whole, word, (a * 2).toFixed(2), a * 2 < 5, a * b + 1);
			try {
				trace(new Thrower() * 2);
			} catch (e:Error) {
				trace("caught", e.message);
			}
		}
	}
}
class Amount {
	private var held:Number;
	public function Amount(p_held:Number) {
		held = p_held;
	}
	public function valueOf():Number {
		return held;
	}
}
class Thrower {
	public function valueOf():Number {
		throw new Error("from valueOf");
	}
}
