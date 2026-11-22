package {
	public class byte_values {
		private var held:byte;
		private function take(x:byte):byte { return x; }
		public function byte_values() {
			var x:byte = 255;
			var boxed:* = -1.9;
			trace(held, byte(), take(boxed), byte(256), byte(Infinity), x + x);
			var wide:Int64 = 9007199254740993;
			var dynamicWide:* = wide;
			trace(byte(wide), byte(dynamicWide), byte("257.9"), byte(NaN), byte(null));
			trace(x.toString(16), x.toFixed(1), x.valueOf());
			x++;
			held = 258;
			trace(x, held);
			var v:Vector.<byte> = new Vector.<byte>(2);
			v[1] = 511;
			v.push(257, -2);
			v.unshift(3);
			trace(v.reverse().join("|"), v.slice(1, 3).join("|"));
			trace(v.splice(1, 2, 260).join("|"), v.pop(), v.shift());
			v.length = 3;
			trace(v.join("|"));
		}
	}
}
