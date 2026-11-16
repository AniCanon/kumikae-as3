package {
	// Loops over vectors that are fields of the object: read and written by index, replaced by a method
	// or through another name for the object in the middle of a function, set to null, made longer, and
	// read past the end. Native code reads such a field without counting a reference where it is used at
	// once, and runs the loop with the vector's length and elements in locals.
	public class field_vectors {
		private var cells:Vector.<int> = new Vector.<int>(6);
		private var weights:Vector.<Number> = new <Number>[0.5, 1.5, 2.5];
		private var total:Number = 0;

		public function field_vectors() {
			fill(3);
			trace(cells, sum());
			trace(scaled(2), weights);
			trace(replaced(), cells);
			var other:field_vectors = this;
			trace(aliased(other), cells.length);
			trace(grown(4), cells);
			try {
				trace(past(9));
			} catch (e:RangeError) {
				trace("past the end", e.errorID, total);
			}
			cells = null;
			try {
				trace(sum());
			} catch (e:TypeError) {
				trace("no vector", e.errorID);
			}
			trace(first(weights), keep().length);
		}

		private function fill(by:int):void {
			for (var i:int = 0; i < cells.length; i++) {
				cells[i] = i * by;
			}
		}

		private function sum():int {
			var all:int = 0;
			for (var i:int = 0; i < cells.length; i++) {
				all += cells[i];
			}
			return all;
		}

		private function scaled(by:Number):Number {
			for (var i:int = 0; i < weights.length; i++) {
				weights[i] = weights[i] * by + cells[i];
				total += weights[i];
			}
			return total;
		}

		private function renew():void {
			cells = new <int>[9, 8, 7];
		}

		// The field is another vector after the call: the loop that follows reads the new one.
		private function replaced():int {
			var before:int = cells[1];
			renew();
			var all:int = before;
			for (var i:int = 0; i < cells.length; i++) {
				all += cells[i];
			}
			return all;
		}

		private function aliased(other:field_vectors):int {
			var all:int = 0;
			for (var i:int = 0; i < cells.length; i++) {
				all += cells[i];
			}
			other.cells = new <int>[1, 1];
			for (var j:int = 0; j < cells.length; j++) {
				all += cells[j];
			}
			return all;
		}

		private function grown(more:int):int {
			for (var i:int = 0; i < more; i++) {
				cells[cells.length] = i;
			}
			return cells.length;
		}

		private function past(n:int):Number {
			total = 0;
			for (var i:int = 0; i < n; i++) {
				total += cells[i];
			}
			return total;
		}

		private function first(of:Vector.<Number>):Number {
			return of[0];
		}

		private function keep():Vector.<Number> {
			return weights;
		}
	}
}
