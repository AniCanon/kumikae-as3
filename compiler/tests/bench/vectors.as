package {
	// A benchmark, not a test: a vector's elements read and written in loops. See run_bench.py.
	public class vectors {
		public function vectors() {
			var cells:Vector.<int> = new Vector.<int>(100000);
			var sum:Number = 0;
			for (var round:int = 0; round < 30; round++) {
				for (var i:int = 0; i < cells.length; i++) {
					cells[i] = cells[i] + i;
				}
				for (var j:uint = 0; j < cells.length; j++) {
					sum += cells[j];
				}
			}
			trace(sum);
		}
	}
}
