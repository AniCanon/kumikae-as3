package {
	// A program that reads an Error's stack keeps a line for every function in progress. The operations
	// native code does itself where it can, and leaves to the library where it cannot, are done once
	// there too: an element added once, a function called once.
	public class stack_fast_paths {
		private static var calls:int = 0;

		private static function bump(value:*):* {
			calls++;
			return value;
		}

		public function stack_fast_paths() {
			var numbers:Vector.<int> = new <int>[1, 2, 3];
			var list:Array = [];
			var holder:Object = {};
			var f:* = bump;
			for (var i:int = 0; i < 4; i++) {
				numbers.push(i);
				list.push("a" + i);
				holder["k" + i] = i;
				f(i);
				numbers[numbers.length] = i;
			}
			var keys:int = 0;
			for (var key:String in holder) {
				keys++;
			}
			trace(numbers.length, numbers.join(","));
			trace(list.length, list.join(","));
			trace(keys, calls, String(f(7)), numbers[2] + list[1]);
			trace(new Error("here").getStackTrace().split("\n").length > 1);
		}
	}
}
