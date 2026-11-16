package {
	// A benchmark, not a test: calls through function values, and objects and arrays made and let go in a
	// loop. See run_bench.py.
	public class values {
		public function values() {
			var add:Function = function (a:Number, b:Number):Number {
				return a + b;
			};
			var sum:Number = 0;
			for (var i:int = 0; i < 300000; i++) {
				sum = add(sum, i);
				var point:Object = { x: i, y: sum };
				var pair:Array = [point.x, i];
				sum += pair.length;
			}
			trace(sum);
		}
	}
}
