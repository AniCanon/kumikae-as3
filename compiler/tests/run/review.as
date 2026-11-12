package {
	public class review extends Root {
		public static var c:int = Late.k + 1;
		private var own:String = note("field of review");
		public var x:int;
		public var n:Number;

		public function review(count:int = 5, label:String = "x") {
			trace("constructor", count, label, n);
			// A built-in error class first named in a later function.
			later(1);
			// Defaults belong to the function that runs, not to the caller's view of it.
			var base:Base = new Derived();
			trace(base.f(), base.f(3), new Base().f(), base.g(), base.g("given"));
			// The receiver is settled before a later operand changes the variable.
			var a:review2 = new review2("A");
			var b:review2 = new review2("B");
			var t:review2 = a;
			t.f(t = b);
			t = a;
			t.x = (t = b, 7);
			trace(a.x, b.x);
			t = a;
			t.x += (t = b, 3);
			trace(a.x, b.x);
			// A class's own toString().
			trace(a, "" + b, String(a), new Plain());
			var none:review2;
			trace(none, "" + none);
			// A missing string in comparisons and conversions.
			var s:String;
			trace(s < "z", s <= "null", s >= "null", s < "5", s <= s, String(s) == null, String(null) + "!", String(s).length);
			// Statics: a base class's are ready before a subclass's.
			trace(c, Late.k, Later.m);
		}
		private function later(n:int):void {
			var e:Error = new RangeError("r");
			var f:Error = new ArgumentError("a");
			trace(n, e, f, e is RangeError, f is RangeError, e as Error);
		}
	}
}
class Root {
	public function Root() { note("constructor of Root"); }
	protected function note(text:String):String { trace(text); return text; }
}
class Base {
	public function f(x:int = 1):int { return x; }
	public function g(s:String = "base"):String { return "Base " + s; }
}
class Derived extends Base {
	override public function f(x:int = 2):int { return x * 10; }
}
class review2 {
	public var x:int;
	private var name:String;
	public function review2(name:String) { this.name = name; }
	public function f(other:review2):void { trace("f on " + name); }
	public function toString():String { return "<" + name + ">"; }
}
class Plain {}
class Later extends Late {
	public static var m:int = k * 2;
}
class Late {
	public static var k:int = 100;
}
