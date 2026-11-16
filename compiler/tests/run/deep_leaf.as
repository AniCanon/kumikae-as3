package {
	// Calls nested to just short of the limit, to it and past it, where the innermost call is of a method
	// that calls nothing and cannot fail: Error #1023 comes at the same depth whatever the innermost is,
	// and two methods that only do sums and call each other for ever end in it too.
	public class deep_leaf {
		private var reached:int = 0;

		public function deep_leaf() {
			for (var depth:int = 9990; depth <= 10002; depth++) {
				try {
					trace(depth, down(depth));
				} catch (e:Error) {
					trace(depth, e.errorID);
				}
			}
			try {
				trace(ping(1));
			} catch (e:Error) {
				trace("for ever", e.errorID, reached > 1000);
			}
		}

		private function leaf(value:int):int {
			return value * 2 + 1;
		}

		private function down(left:int):int {
			if (left == 0) {
				return leaf(1);
			}
			return down(left - 1) + 1;
		}

		private function ping(value:Number):Number {
			reached++;
			return pong(value + 1) * 2;
		}

		private function pong(value:Number):Number {
			return ping(value - 0.5) + 1;
		}
	}
}
