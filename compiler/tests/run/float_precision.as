package {
	public class float_precision {
		private var held:float;
		private static var shared:float;
		private function identity(x:float):float { return x; }
		public function float_precision() {
			var a:float = 16777217;
			var one:float = float(1);
			trace(held, a, a + one, a + 1, a + Number(1));
			var boxed:* = a;
			trace(identity(boxed), identity(16777217), float(-0), 1 / float(-0));
			var values:Vector.<float> = new Vector.<float>(2);
			values[1] = 16777217;
			trace(values[0], values[1]);
			trace(shared, float(), float(null), 1 / float(1e-50), float(1e40));
			held = a + 3;
			shared = held;
			trace(shared, (a + one) - a, (a + Number(1)) - a);
			values.push(3.5, -2.25);
			values.unshift(5);
			trace(values.reverse().join("|"));
			trace(values.slice(1, 3).join("|"), values.splice(2, 1, 7.25).join("|"));
			trace(values.pop(), values.shift(), values.join("|"));
			values.length = 4;
			trace(values[3], values.indexOf(7.25));
			var wide:Int64 = 4611686293305294849;
			var boxedWide:* = wide;
			var unset:float;
			trace(float(boxedWide) > 4611686018427387904, unset, float(1.25).toFixed(1), typeof one, boxed is Number);
			trace(float(0) || 16777217, float(1) && Number(16777217));
			trace(one / float(10) == float(0.1), float(5.75) % float(2), float(1.5) * float(3));
			trace(1 / float(-1e-60), float(1.401298464324817e-45) > 0);
			trace(values.map(function(x:float, i:int, all:Vector.<float>):float { return x + float(1); }).join("|"));
			trace(values.sort(16).join("|"));
			trace(float(wide) > 4611686018427387904, wide > float(1), wide != float(1));
		}
	}
}
