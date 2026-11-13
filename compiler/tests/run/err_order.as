package {
	public class err_order {
		private var log:String = "";
		public function err_order() {
			var nobody:Target;
			trace(Number.MAX_VALUE > 1e300, int.MAX_VALUE, int.MIN_VALUE, uint.MAX_VALUE, NaN, Infinity, -Infinity, Number.NaN);
			trace(Number.MIN_VALUE, Number.NEGATIVE_INFINITY, Number.POSITIVE_INFINITY, Number.MAX_VALUE, uint.MIN_VALUE);
			// The arguments of a call are evaluated before the missing object is reported, as in Flash.
			nobody.hit(note("first"), note("second"));
			trace("not reached");
		}
		private function note(text:String):int {
			trace("evaluated", text);
			return 1;
		}
	}
}
class Target {
	public function hit(a:int, b:int):void {}
}
