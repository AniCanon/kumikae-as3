package {
	public class small_fixes {
		public function small_fixes() {
			// typeof a String that holds null is "object", as for null itself.
			var nothing:String = null;
			var some:String = "s";
			trace(typeof nothing, typeof some);
			// continue to the outer of two labels on one loop.
			var seen:String = "";
			a: b: for (var i:int = 0; i < 3; i++) {
				if (i == 1) {
					continue a;
				}
				seen += i;
			}
			trace(seen);
			// Two untyped integers added stay an integer when the sum fits.
			var x:* = 5;
			var y:* = x + 1;
			var big:* = 2147483647;
			var past:* = big + 1;
			trace(y, typeof y, y is int, past, past is int);
		}
	}
}
