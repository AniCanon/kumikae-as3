package {
	public class LocalOperands {
		public function LocalOperands() {
			trace(square(2.5), add(1.5, 2.25), fields(), accumulate(2147483647));
		}

		private function square(value:Number):Number {
			var n = value;
			return n * n;
		}

		private function add(left:Number, right:Number):Number {
			var x = left;
			var y = right;
			return x + y;
		}

		private function fields():Number {
			var p = new Pair();
			return p.x + p.y;
		}

		private function accumulate(i:int):Number {
			var sum = 0;
			sum += i;
			sum += i;
			return sum;
		}
	}
}

final class Pair {
	public var x:Number = 3;
	public var y:Number = 4;
}
