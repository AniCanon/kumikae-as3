package {
	// A benchmark, not a test: arithmetic, calls and fields in a loop. See run_bench.py.
	public class loop {
		private var total:Number = 0;
		private var steps:int = 0;

		public function loop() {
			var checksum:int = 0;
			for (var i:int = 0; i < 3000000; i++) {
				total += scaled(i, 0.5);
				if ((i & 1023) == 0) {
					checksum ^= bump(i);
				}
			}
			trace(total, steps, checksum);
		}

		private function scaled(value:int, by:Number):Number {
			return value * by + 1;
		}

		private function bump(value:int):int {
			steps++;
			return value % 97 + steps;
		}
	}
}
