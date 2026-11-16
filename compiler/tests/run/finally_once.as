package {
	public class finally_once {
		public function finally_once() {
			trace(pick(0), pick(1), pick(2));
		}

		private function pick(n:int):String {
			for (var i:int = 0; i < 3; i++) {
				try {
					if (n == 0) {
						return "zero";
					}
					if (n == 1) {
						break;
					}
					if (i == 0) {
						continue;
					}
					return "two";
				} finally {
					trace("cleanup marker");
				}
			}
			return "after";
		}
	}
}
