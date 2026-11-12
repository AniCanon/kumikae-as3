package {
	public class numbers {
		public function numbers() {
			// Formatting.
			trace(1 / 3, 0.1 + 0.2, 100, 1.0, -0.0, 1e21, 1e-7, 123456789012345680000);
			trace(1 / 0, -1 / 0, 0 / 0, 5 % 0, -7 % 3, 7 % -3, 5.5 % 2);
			// int and uint wrap when a value is stored in them.
			var i:int = 2147483647;
			i++;
			trace(i);
			i = i - 1;
			trace(i);
			var u:uint = 0;
			u--;
			trace(u);
			u += 2;
			trace(u);
			var big:int = 1e10;
			var half:int = 7 / 2;
			var negative:int = -7 / 2;
			trace(big, half, negative, 7 / 2);
			i = 46341;
			i = i * i;
			trace(i, 46341 * 46341);
			i = 5;
			i /= 2;
			trace(i);
			i *= 1.5;
			trace(i);
			// Conversions written as calls.
			trace(int(3.99), int(-3.99), int("12"), int("abc"), int(true), uint(-1), Number("1.5"), Number(""), Number("x"));
			trace(Boolean(0), Boolean(1), Boolean(""), Boolean("0"), Boolean(0 / 0), Boolean("false"));
			trace(String(1.5), String(true), String(10) + 1);
			// Bits.
			trace(1 << 31, 1 << 32, -1 >>> 0, -1 >> 1, -8 >>> 28, 5 & 3, 5 | 3, 5 ^ 3, ~5, 2147483648 | 0);
			trace(1.9 | 0, -1.9 | 0, 3000000000 >> 0, 1 << 2.9);
			// Defaults.
			var n:Number;
			var j:int;
			var v:uint;
			var b:Boolean;
			trace(n, j, v, b, n == n, n != n);
			// Comparison.
			trace(1 < 2, 2 <= 2, 3 > 4, 0 / 0 < 1, 0 / 0 >= 1, 1 == 1.0, i == u, -0.0 == 0);
			trace(0.1 * 3, 9007199254740993, 2147483647 + 1, 4294967295 + 1, -2147483648 - 1);
			var x:Number = 10;
			x -= 2.5;
			x *= 2;
			x /= 4;
			x %= 2;
			trace(x, -x, +x, x++ + x, x, --x);
		}
	}
}
