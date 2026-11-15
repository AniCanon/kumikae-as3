package {
	public class wide_mixed {
		public function wide_mixed() {
			var w:Int64 = 9007199254740993;
			var one:Int64 = 1;
			var c:Boolean = true;
			// A written cast cuts each branch of a `?:`, whatever the branches join to.
			trace(int(c ? w : 1.5), int(w), Number(c ? w : 1.5));
			// Against a Boolean or a String: exactly, as numbers.
			trace(w == true, one == true, true == one, w != "9007199254740993", w == "9007199254740993",
					"9007199254740992" < w, w < "9007199254740994", w >= "1e30", w | true, w & "3");
			// Text that is no whole number an Int64 holds is a RangeError where nothing is written.
			try { trace(w & "99999999999999999999"); } catch (e:RangeError) { trace("RangeError"); }
			trace(w & Int64("99999999999999999999"));
			// A shift is as wide as what is shifted: an int shifted by an Int64 count takes its low five bits.
			var i:int = -8;
			var count:Int64 = 33;
			trace(i >>> count, i << count, i >> count, w >>> 1);
			var j:int = 1;
			j <<= count;
			trace(j);
		}
	}
}
