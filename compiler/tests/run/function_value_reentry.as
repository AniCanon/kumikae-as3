package { public class function_value_reentry {
	// Making each argument a String runs its toString(), which runs deep enough for the stack to grow: the
	// arguments after it are still read where they are.
	public function function_value_reentry() {
		var f:Function = both;
		trace(f(new Deep(40), new Deep(40)));
	}
	private function both(a:String, b:String):String { return a + "|" + b; }
} }
class Deep {
	private var depth:int;
	public function Deep(p_depth:int) { depth = p_depth; }
	public function toString():String { return depth > 0 ? String(new Deep(depth - 1)) : "end"; }
}
