package { public class function_values { private var base:int = 10;
	public function function_values() {
		var f:Function = add;
		trace(f(1, 2), typeof f, f == add);
		var g:* = this.add;
		trace(g(3, 4));
		var h:Function = twice;
		trace(h(5), h("6"));
		try { f(1); } catch (e:ArgumentError) { trace(e.message); }
		var n:Function = null;
		try { n(); } catch (e:TypeError) { trace("null function"); }
		var o:Other = new Other();
		var v:Function = o.speak;
		trace(v());
	}
	public function add(a:int, b:int):int { return a + b + base; }
	private static function twice(x:Number):Number { return x * 2; }
} }
class Other { public function speak():String { return "other"; } }
