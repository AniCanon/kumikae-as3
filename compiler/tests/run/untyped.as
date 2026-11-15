package {
	public class untyped {
		private var held:*;
		private var thing:Object;
		public static var shared:*;

		public function untyped() {
			var a:*;
			var o:Object;
			trace(a, o, a == o, a === o, held, thing, shared);
			trace(typeof a, typeof o, typeof 5, typeof "s", typeof true);

			a = 5;
			trace(a, typeof a, a is int, a is Number, a is String);
			a = 5.5;
			trace(a, a is int, a is Number);
			a = "12";
			trace(a, typeof a, a is String, a + 1, a - 1, a * 2);
			a = true;
			trace(a, typeof a, a is Boolean, a + 1);
			a = null;
			trace(a, typeof a, a == undefined, a === undefined, a == null);

			var n:int = 0;
			var x:* = "7";
			n = x;
			var u:uint = x;
			var num:Number = x;
			var b:Boolean = x;
			var s:String = x;
			trace(n, u, num, b, s);
			x = null;
			s = x;
			trace(s, s == null, String(x), "" + x);
			x = undefined;
			s = x;
			n = x;
			num = x;
			b = x;
			trace(s, n, num, b, String(x), "[" + x + "]");

			var first:* = 3;
			var second:* = "3";
			trace(first == second, first === second, first != second, first !== second);
			trace(first + second, second + first, first + 4, 1 + first);
			trace(first < 4, second < "4", "10" < "9", first > second, first <= 3, first >= 3.5);

			held = new Item("held");
			thing = held;
			trace(held, thing, held == thing, held === thing, held is Item, thing is Item, typeof held);
			var back:Item = thing as Item;
			trace(back.name, held as String, (held as Item).name, thing as String);
			var kept:Item = held;
			trace(kept.name);

			o = 42;
			trace(o, o is int, typeof o, o == 42);
			shared = 1;
			trace(shared, sum(1, "2", null), sum(true, false, 2.5));
			trace(echo(7), echo("seven"), echo(null), echo(undefined), echo(new Item("e")));
			var c:* = 0;
			var none:*;
			trace(c ? "yes" : "no", none ? "yes" : "no", !c, !none, c || "fallback", none && "never");
			if (c) {
				trace("never");
			} else {
				trace("zero is false");
			}
			trace(int("12abc"), Number(held), String(held), Boolean(held));
		}

		private function sum(a:*, b:*, c:*):* {
			return a + b + c;
		}

		private function echo(value:Object):Object {
			return value;
		}
	}
}

class Item {
	public var name:String;
	public function Item(name:String) {
		this.name = name;
	}
	public function toString():String {
		return "Item(" + name + ")";
	}
}
