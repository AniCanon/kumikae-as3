package {
	// A benchmark, not a test: objects of a class that holds numbers and a String, made, used and let go in a loop. See run_native_bench.py.
	public class objects {
		public function objects() {
			var sum:Number = 0;
			for (var i:int = 0; i < 2000000; i++) {
				var p:Point = new Point(i, 0.5);
				sum += p.length2();
			}
			trace(sum);
		}
	}
}

class Point {
	public var x:Number;
	public var y:Number;
	public var name:String = "p";

	public function Point(p_x:Number, p_y:Number) {
		x = p_x;
		y = p_y;
	}

	public function length2():Number {
		return x * x + y * y;
	}
}
