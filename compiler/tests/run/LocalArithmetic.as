package {
	public class LocalArithmetic {
		public function LocalArithmetic() {
			trace(total(3.5, true), total(-2, false));
			trace(total(2147483647, true));
		}

		private function twice(value:Number):Number { return value * 2; }

		private function total(source:Number, enabled:Boolean):Number {
			var n:* = source;
			var b:* = enabled;
			var box:* = new Amount();
			var sum:Number = box.value + 1;
			if (b && n > 0) sum += n;
			if (b) sum += twice(n);
			return sum + n * 2;
		}
	}
}

final class Amount {
	public var value:int = 3;
}
