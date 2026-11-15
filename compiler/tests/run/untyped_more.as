package {
	public class untyped_more {
		private var field:Object = undefined;
		private static var shared:Object = undefined;
		private var seen:String = "";

		public function untyped_more() {
			// switch on an untyped value compares as === does.
			trace(pick(5), pick("x"), pick(null), pick(5.0), pick("5"), pick(undefined));
			// Compound assignment with an untyped side works out what it holds now.
			var s:* = "a";
			s += 1;
			var n:* = 1;
			n += "b";
			var m:* = 2;
			m += 3;
			var text:String = "a";
			var u:*;
			text += u;
			var u2:*;
			u2 += "y";
			var k:int = 1;
			var t:* = "2";
			k += t;
			m -= "1";
			trace(s, n, m, text, u2, k);
			// A script object as a number: its text, then that as a number.
			var five:* = new Five();
			var asNumber:Number = five;
			var asInt:int = five;
			trace(five * 2, Number(five), asNumber, asInt, five == 5, five < 6);
			// An Object place never holds undefined.
			trace(field, shared, nothing(false));
			// `a > b` puts a into words before b, as ECMAScript says.
			var a:* = new Loud("a", this);
			var b:* = new Loud("b", this);
			var bigger:Boolean = a > b;
			var notLess:Boolean = a >= b;
			trace(seen, bigger, notLess);
		}

		public function note(what:String):void {
			seen += what;
		}

		private function pick(value:*):String {
			switch (value) {
				case 5:
					return "five";
				case "x":
					return "x";
				case null:
					return "null";
				default:
					return "default";
			}
		}

		private function nothing(answer:Boolean):Object {
			if (answer) {
				return 1;
			}
		}
	}
}

class Five {
	public function toString():String {
		return "5";
	}
}

class Loud {
	private var name:String;
	private var owner:untyped_more;
	public function Loud(name:String, owner:untyped_more) {
		this.name = name;
		this.owner = owner;
	}
	public function toString():String {
		owner.note(name);
		return name;
	}
}
