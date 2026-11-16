package {
	// Static fields read in loops. Where nothing in a function can write one, native code reads it once as
	// the function begins; where the function writes it itself, or calls something that does, or first uses
	// the class it belongs to, every read must see the value as it is then.
	public class static_still {
		private static const LIMIT:int = 5;
		private static var step:int = 2;
		private static var scale:Number = 0.5;
		private static var on:Boolean = true;
		private var values:Vector.<Number> = new Vector.<Number>(8, true);

		public function static_still() {
			trace(quiet(), writes(), calls(), other(), quiet());
			step = 3;
			scale = 2;
			trace(quiet(), fills());
			on = false;
			trace(quiet());
		}

		// Nothing here can write a static field.
		private function quiet():Number {
			var total:Number = 0;
			for (var i:int = 0; i < LIMIT; i += 1) {
				if (on) {
					total += i * step * scale;
				}
			}
			return total;
		}

		private function fills():Number {
			var total:Number = 0;
			for (var i:int = 0; i < LIMIT; i++) {
				values[i] = i * scale;
				total += values[i] + step;
			}
			return total;
		}

		// Writes the field it reads, in the loop.
		private function writes():int {
			var total:int = 0;
			for (var i:int = 0; i < LIMIT; i++) {
				total += step;
				step += 1;
			}
			step = 2;
			return total;
		}

		// Calls a function that writes it.
		private function calls():int {
			var total:int = 0;
			for (var i:int = 0; i < LIMIT; i++) {
				total += step;
				bump();
			}
			step = 2;
			return total;
		}

		private function bump():void {
			step += 10;
		}

		// A static field of another class, whose first use is in the loop.
		private function other():int {
			var total:int = 0;
			for (var i:int = 0; i < LIMIT; i++) {
				total += Late.count + Late.next();
			}
			return total;
		}
	}
}

class Late {
	public static var count:int = 7;

	public static function next():int {
		count += 1;
		return count;
	}
}
