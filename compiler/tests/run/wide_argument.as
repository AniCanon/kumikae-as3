package {
	// A whole number past an int, given through a function value to a Number parameter: the Number it is,
	// and an Int64 past what a Number holds exactly is the nearest Number, as a store into one through an
	// untyped value makes it.
	public class wide_argument {
		public function wide_argument() {
			var add:Function = function (a:Number, b:Number):Number {
				return a + b;
			};
			var sum:Number = 2147483647;
			sum = add(sum, 1);
			trace(sum);
			sum = add(sum, sum);
			trace(sum);
			var far:Int64 = 9007199254740993;
			var untyped:* = far;
			trace(add(untyped, 0));
			var shown:Function = function (a:int, b:uint):String {
				return a + " " + b;
			};
			trace(shown(untyped, untyped));
		}
	}
}
