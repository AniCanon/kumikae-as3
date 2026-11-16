package {
	// Sums and differences of ints and uints, at the ends of their ranges: an int wraps at 2^31 and a uint
	// at 2^32, whatever is added, and a Number in between is never cut short.
	public class whole_sums {
		private var steps:int = 2147483646;
		private var count:uint = 4294967294;

		public function whole_sums() {
			var i:int = 2147483647;
			i++;
			trace(i);
			i--;
			trace(i);
			var u:uint = 0;
			u--;
			trace(u);
			u++;
			trace(u);
			var a:int = -2147483648;
			var b:int = -2147483648;
			var sum:int = a + b;
			var wide:Number = a + b;
			trace(sum, wide);
			var c:uint = 4294967295;
			var d:uint = 4294967295;
			var both:uint = c + d;
			var mixed:int = c + a;
			var back:uint = a - c;
			trace(both, mixed, back, c + d, c + a, a - c);
			var three:int = a + b + a;
			var many:int = i + i + i + i - a - a + 5 - 7;
			trace(three, many, i + i + i + i - a - a + 5 - 7);
			steps++;
			trace(steps);
			steps++;
			trace(steps);
			steps += 2147483647;
			trace(steps);
			count += 3;
			trace(count);
			count -= 5;
			trace(count);
			// The registers a sum was made from change before it is read again.
			var x:int = 10;
			var n:Number = x + 1;
			x = 100;
			var y:int = n;
			var z:int = x + 1;
			trace(y, z);
			var total:int = 0;
			for (var k:int = 2147483640; k != -2147483640; k++) {
				total += k;
			}
			trace(total, k);
			var down:uint = 3;
			while (down != 4294967293) {
				down--;
			}
			trace(down);
			var half:int = 7 + 0.5;
			var minus:int = x + -0.0;
			var big:int = x + 4294967295;
			var bigger:int = x + 4294967296;
			trace(half, minus, big, bigger);
		}
	}
}
