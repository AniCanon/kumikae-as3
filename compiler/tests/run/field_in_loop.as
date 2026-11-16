package {
	// A field read and written in a loop, and everything else that may see or change it meanwhile: a
	// method that reads it, one that writes it, the same object by another name, a function value, an
	// error in the middle of the loop, a way out of the middle of it. Native code may keep such a field in
	// a local while the loop runs; what the program sees must not change.
	public class field_in_loop {
		private var total:Number = 0;
		private var count:int = 0;
		private var bits:uint = 0;
		private var odd:Boolean = false;
		private var items:Vector.<int> = new <int>[1, 2, 3];

		public function field_in_loop() {
			trace(plain(10), total, count, bits, odd);
			trace(thenRead(5), total);
			trace(thenWrite(5), total);
			var other:field_in_loop = this;
			trace(alias(other, 4), total);
			try {
				failing(8);
			} catch (e:Error) {
				trace("caught", total, count);
			}
			trace(caughtInside(6), total);
			trace(callback(3), total);
			trace(early(100), total, count);
			trace(survives(4), total);
			try {
				leaves(2);
			} catch (e:Error) {
				trace(e.message, total);
			}
			trace(twice(3), total, count);
			trace(inner(4), total);
		}

		// Nothing else sees the fields while the loop runs.
		private function plain(n:int):Number {
			for (var i:int = 0; i < n; i++) {
				total += i;
				count++;
				bits |= 1 << i;
				odd = !odd;
			}
			return total;
		}

		private function read():Number {
			return total * 2;
		}

		private function write(value:Number):void {
			total = value;
		}

		private function half(value:Number):Number {
			return value / 2;
		}

		private function thenRead(n:int):Number {
			for (var i:int = 0; i < n; i++) {
				total += 1;
			}
			var seen:Number = read();
			total += 1;
			return seen + total;
		}

		private function thenWrite(n:int):Number {
			for (var i:int = 0; i < n; i++) {
				total += 1;
			}
			write(100);
			total += 1;
			return total;
		}

		private function alias(other:field_in_loop, n:int):Number {
			for (var i:int = 0; i < n; i++) {
				total += 1;
			}
			other.total = 7;
			total += 1;
			var seen:Number = other.total;
			return seen + total;
		}

		// The fourth element is not there: the loop is left by a RangeError with three of them added.
		private function failing(n:int):void {
			for (var i:int = 0; i < n; i++) {
				total += items[i];
				count++;
			}
		}

		private function caughtInside(n:int):Number {
			try {
				for (var i:int = 0; i < n; i++) {
					total += items[i];
				}
			} catch (e:RangeError) {
				total += 1000;
			}
			return total;
		}

		private function callback(n:int):Number {
			var set:Function = function ():void {
				total = 50;
			};
			for (var i:int = 0; i < n; i++) {
				total += 1;
			}
			set();
			total += 1;
			return total;
		}

		private function early(n:int):Number {
			for (var i:int = 0; i < n; i++) {
				total += 1;
				count++;
				if (total > 60) {
					return total;
				}
			}
			return -1;
		}

		private function writeThenThrow():void {
			total = 500;
			throw new Error("after writing");
		}

		private function survives(n:int):Number {
			for (var i:int = 0; i < n; i++) {
				total += 1;
			}
			try {
				writeThenThrow();
			} catch (e:Error) {
			}
			return total;
		}

		private function leaves(n:int):void {
			for (var i:int = 0; i < n; i++) {
				total += 1;
			}
			writeThenThrow();
		}

		// Two loops: the second calls a method that reads the field.
		private function twice(n:int):Number {
			var seen:Number = 0;
			for (var i:int = 0; i < n; i++) {
				total += 1;
			}
			for (var j:int = 0; j < n; j++) {
				count += 2;
				seen += read();
				total += 1;
			}
			return seen;
		}

		// A loop that calls a method which touches no field.
		private function inner(n:int):Number {
			for (var i:int = 0; i < n; i++) {
				total += half(i);
			}
			return total;
		}
	}
}
