package {
	public class int64 {
		private var held:Int64 = 5;
		private var unset:Int64;
		private static var shared:Int64 = Int64.MAX_VALUE;

		public function int64() {
			var max:Int64 = Int64.MAX_VALUE;
			var min:Int64 = Int64.MIN_VALUE;
			var big:Int64 = 9007199254740993; // 2^53 + 1: no Number holds it.
			var w:Int64 = 1;
			var one:int = 1;
			var u:uint = 4000000000;
			var n:Number = 2.5;
			// Text.
			trace(max, min, big, unset, 0x7FFFFFFFFFFFFFFF == max);
			trace("" + big, String(min), "<" + w + ">");
			var written:Int64 = -9223372036854775808;
			trace(written == min, written);
			// Arithmetic stays in 64 bits and wraps at the ends.
			trace(max + 1 == min, min - 1 == max, min * -1 == min, -min == min);
			trace(big + one, big - u, big * 2, big % 1000, -big, +big);
			trace(min % -1, -7 % w, big % -1000, (0 - big) % 1000);
			trace(big & 0xFF, big | 6, big ^ big, ~big, big << 3, big >> 3);
			trace(min >> 60, min >>> 60, w << 40, w << 64, w << 65, w << -1, (0 - w) >>> 1 == max);
			// Division is a Number, as always in AS3; so is anything with a Number.
			trace(w / 2, Number(max) / Number(max), (w << 53) / 2, w + n, w * n, n - w, (w << 53) + 0.5);
			// Comparisons are exact.
			trace(big > 9007199254740992, big == 9007199254740992, big != 9007199254740992, big < 9007199254740994);
			trace(max < 9223372036854775808, max >= 9223372036854775808, min == -9223372036854775808, min <= -9223372036854775808);
			trace(w == one, w === one, w != one, u < big, big > one, one <= w, w >= u);
			var nan:Number = 0 / 0;
			trace(w == nan, w != nan, w < nan, w >= nan, max < 1 / 0, min > -1 / 0);
			trace(w < n, n < w, w <= 1.0, 1.0 >= w, w > 0.5);
			// A conversion that is not written checks the range; in range it is exact.
			var i:int = w + 41;
			var back:uint = big - 9007199254740000;
			var asNumber:Number = w << 53;
			var fromNumber:Int64 = 3.99;
			var fromNegative:Int64 = -3.99;
			var fromUint:Int64 = u;
			var fromInt:Int64 = -one;
			trace(i, back, asNumber, fromNumber, fromNegative, fromUint, fromInt);
			// A conversion that is written is AS3's own: it cuts, rounds or holds at the end.
			trace(int(max), uint(max), int(big), int(min), uint(min + 5), Number(big), Number(max), Number(min));
			trace(Int64(1e30), Int64(-1e30), Int64(nan), Int64(2.9), Int64(-2.9), Int64(u), Int64(true));
			trace(Int64("123456789012345678"), Int64("-5"), Int64("12.7"), Int64("abc"), Int64(" 42 "), Int64("0x10"));
			// Fields, statics, parameters, defaults, results.
			held += 1;
			held++;
			++held;
			held--;
			held *= held;
			trace(held, held++, held, --held, shared, twice(held), twice(held, 3), twice(one));
			shared -= 1;
			shared >>= 1;
			held <<= 2;
			held %= 7;
			held |= 8;
			trace(shared, held);
			i += w;
			i *= w + 1;
			n += w;
			trace(i, n);
			var chosen:Int64 = one > 0 ? big : one;
			trace(chosen, one < 0 ? big : one, w ? "set" : "zero", !unset, unset ? "set" : "zero");
			for (var count:Int64 = 0; count < 3; count++) {
				trace(count);
			}
		}

		private function twice(x:Int64, by:Int64 = 2):Int64 {
			return x * by;
		}
	}
}
