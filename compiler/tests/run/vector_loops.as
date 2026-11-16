package {
	// Loops over vectors of numbers, and what may happen in the middle of one: an index past the end read
	// (a RangeError, with what was done before it kept), a vector that grows as the loop writes at its end,
	// a fixed one that may not, a negative index, no vector at all, two names for one vector, a loop inside
	// a loop, and a method called from the loop. Native code runs such a loop with the vector's length and
	// elements in locals for as long as every index is in range, and as it always did from the first that
	// is not.
	public class vector_loops {
		public function vector_loops() {
			var cells:Vector.<int> = new Vector.<int>(8);
			var i:int;
			for (i = 0; i < cells.length; i++) {
				cells[i] = i * i;
			}
			trace(cells);
			trace(sum(cells, 8), sum(cells, 3));
			try {
				trace(sum(cells, 10));
			} catch (e:RangeError) {
				trace("past the end", e.errorID, seen);
			}
			var grown:Vector.<Number> = new Vector.<Number>();
			for (i = 0; i < 6; i++) {
				grown[i] = i / 2;
			}
			trace(grown, grown.length);
			var fixed:Vector.<uint> = new Vector.<uint>(3, true);
			try {
				for (var j:uint = 0; j < 5; j++) {
					fixed[j] = j + 1;
				}
			} catch (e:RangeError) {
				trace("fixed", e.errorID, fixed);
			}
			try {
				for (i = 2; i > -3; i--) {
					cells[i] = cells[i] + 100;
				}
			} catch (e:RangeError) {
				trace("negative", e.errorID, cells);
			}
			var none:Vector.<int> = null;
			try {
				trace(sum(none, 0));
				trace(sum(none, 1));
			} catch (e:TypeError) {
				trace("no vector", e.errorID);
			}
			var same:Vector.<int> = cells;
			for (i = 0; i + 1 < same.length; i++) {
				cells[i + 1] = same[i] + cells[i + 1];
			}
			trace(cells);
			var flags:Vector.<Boolean> = new Vector.<Boolean>(5);
			var count:int = 0;
			for (i = 0; i < flags.length; i++) {
				flags[i] = (i & 1) == 0;
				if (flags[i]) {
					count++;
				}
			}
			trace(flags, count);
			var grid:Vector.<Number> = new Vector.<Number>(12);
			for (var row:int = 0; row < 3; row++) {
				for (var column:int = 0; column < 4; column++) {
					grid[row * 4 + column] = half(row) + column;
				}
			}
			trace(grid);
			trace(appended(5));
		}

		private var seen:int = 0;

		private function sum(from:Vector.<int>, n:int):int {
			var total:int = 0;
			seen = 0;
			for (var i:int = 0; i < n; i++) {
				total += from[i];
				seen++;
			}
			return total;
		}

		private function half(value:Number):Number {
			return value / 2;
		}

		// Reads what it wrote a moment ago, at the end it keeps moving.
		private function appended(n:int):Number {
			var out:Vector.<Number> = new <Number>[1];
			for (var i:int = 1; i < n; i++) {
				out[out.length] = out[i - 1] * 2;
			}
			var total:Number = 0;
			for (var k:int = 0; k < out.length; k++) {
				total += out[k];
			}
			return total;
		}
	}
}
