package { public class function_call_apply {
	private var base:int = 100;
	public function function_call_apply() {
		var f:Function = add;
		trace(f.call(null, 1, 2), f.call(this, 3, 4), f.apply(null, [5, 6]), f.apply(this, [7, 8]));
		var g:Function = function (a:int, b:int = 1, ...more):int { return a + b + more.length; };
		trace(g.call(null, 1), g.apply(null, [1, 2, 3, 4]), g.apply(null, [5]), g.length, f.length);
		var h:Function = join;
		trace(h.apply(null, ["x", "y"]), h.call(undefined));
		try { f.apply(null, [1]); } catch (e:ArgumentError) { trace("ArgumentError"); }
		try { f.apply(null, 5); } catch (e:TypeError) { trace(e.message); }
		try { f.apply(null, new <int>[7, 8]); } catch (e:TypeError) { trace("TypeError"); }
		var n:Function = null;
		try { n.call(null); } catch (e:TypeError) { trace("null"); }
	}
	private function add(a:int, b:int):int { return a + b + base; }
	private static function join(...parts):String { return parts.join("+"); }
} }
