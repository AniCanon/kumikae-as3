package frames {
	import godot.Node2D;

	public class Sieve extends Node2D {
		private static const LIMIT:int = 200000;

		private var flags:Vector.<int> = new Vector.<int>(LIMIT, true);
		private var total:int = 0;

		override public function _process(delta:Number):void {
			var i:int;
			for (i = 0; i < LIMIT; i++) {
				flags[i] = 1;
			}
			var count:int = 0;
			for (i = 2; i < LIMIT; i++) {
				if (flags[i] == 1) {
					count++;
					var j:int = i + i;
					while (j < LIMIT) {
						flags[j] = 0;
						j += i;
					}
				}
			}
			total += count;
		}

		public function checksum():Number {
			return total;
		}
	}
}
