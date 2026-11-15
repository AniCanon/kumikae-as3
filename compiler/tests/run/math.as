package {
	public class math {
		public function math() {
			trace(Math.E, Math.LN10, Math.LN2, Math.LOG10E, Math.LOG2E, Math.PI, Math.SQRT1_2, Math.SQRT2);
			trace(Math.abs(-2.5), Math.abs(3), Math.abs(-0.0), Math.abs(NaN));
			trace(Math.acos(1), near(Math.acos(0.5)), near(Math.asin(1)), near(Math.atan(1)), Math.acos(2));
			trace(Math.ceil(1.2), Math.ceil(-1.2), Math.floor(1.8), Math.floor(-1.2), Math.ceil(-0.5));
			trace(Math.cos(0), Math.sin(0), Math.tan(0), near(Math.cos(Math.PI)), near(Math.sin(Math.PI / 2)), near(Math.tan(1)));
			trace(Math.exp(0), near(Math.exp(1)), Math.log(1), near(Math.log(Math.E)), Math.log(0), Math.log(-1));
			trace(Math.sqrt(16), Math.sqrt(2), Math.sqrt(-1));
			trace(Math.round(2.5), Math.round(-2.5), Math.round(2.4), Math.round(-2.6), Math.round(0.49999999999999994));
			trace(1 / Math.round(-0.2), 1 / Math.round(-0.5), Math.round(1e300), Math.round(NaN));
			trace(near(Math.atan2(1, 1)), near(Math.atan2(0, -1)), near(Math.atan2(-1, 0)), Math.atan2(0, 0));
			trace(Math.pow(2, 10), near(Math.pow(2, 0.5)), Math.pow(-8, 1 / 3), Math.pow(0, 0), Math.pow(NaN, 0));
			trace(Math.pow(1, Infinity), Math.pow(-1, -Infinity), Math.pow(1, NaN), Math.pow(2, -1));
			trace(Math.max(1, 2), Math.max(3, 2, 9, 4), Math.max(5), Math.max(), Math.max(1, NaN, 3));
			trace(Math.min(1, 2), Math.min(3, 2, 9, -4), Math.min(5), Math.min(), Math.min(1, NaN, 3));
			trace(1 / Math.min(0, -0.0), 1 / Math.max(0, -0.0), 1 / Math.max(-0.0, -0.0));
			var i:int = Math.floor(7.9);
			var u:uint = Math.max(2, 3);
			var n:int = 7;
			trace(i, u, Math.sqrt(n * n), Math.max(n, 2.5), Math.abs(n - 10));
			var inside:Boolean = true;
			for (var k:int = 0; k < 1000; k++) {
				var r:Number = Math.random();
				inside = inside && r >= 0 && r < 1;
			}
			trace(inside, Math.random() != Math.random());
			trace(order());
		}

		// The last digit of a sine or a logarithm is the platform's own: nine places are compared.
		private function near(n:Number):Number {
			return Math.round(n * 1e9) / 1e9;
		}

		private var seen:String = "";
		private function mark(n:Number):Number {
			seen += n;
			return n;
		}
		// Arguments are worked out left to right, once each.
		private function order():String {
			Math.max(mark(1), mark(2), mark(3));
			Math.pow(mark(4), mark(5));
			Math.atan2(mark(6), mark(7));
			return seen;
		}
	}
}
