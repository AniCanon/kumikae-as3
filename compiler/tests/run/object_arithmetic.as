package {
	public class object_arithmetic {
		public function object_arithmetic() {
			// An Object holds anything, as a `*` does: what it holds decides.
			var o:Object = 5;
			var t:Object = "five";
			trace(o + 1, o * 2, -o, t + 1, o - 1, o < 6);
			o += 2;
			trace(o);
			var n:Number = o + 0.5;
			trace(n);
		}
	}
}
