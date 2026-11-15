package {
	public class untyped_as {
		public function untyped_as() {
			var five:* = 5;
			var half:* = 2.5;
			var word:* = "w";
			var yes:* = true;
			// `x as int` is the value if it is one; otherwise what an int holds for null, 0.
			var i:int = five as int;
			var j:int = word as int;
			var u:uint = half as uint;
			var n:Number = word as Number;
			var m:Number = half as Number;
			var b:Boolean = yes as Boolean;
			var c:Boolean = five as Boolean;
			trace(i, j, u, n, m, b, c);
			trace(five is int, half is int, word is Number, yes is Boolean);
		}
	}
}
