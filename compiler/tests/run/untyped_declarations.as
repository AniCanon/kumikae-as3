package {
	public class untyped_declarations {
		// No type written: `*`, as in Flash (a warning says so).
		var field;
		public function untyped_declarations() {
			var x:* = 1;
			var y = 2;
			y = "two";
			var never;
			trace(x, y, never, field, plusOne(3), plusOne("a"), nothing());
		}
		private function plusOne(value) {
			return value + 1;
		}
		private function nothing() {
		}
	}
}
