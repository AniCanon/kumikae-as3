package {
	// `is`, `as` and `catch` with Int64: a whole number an Int64 holds, whatever it is held as.
	public class catch_int64 {
		private function thrown(value:*):String {
			try {
				throw value;
			} catch (e:String) {
				return "String " + e;
			} catch (e:Int64) {
				return "Int64 " + e;
			} catch (e:*) {
				return "other " + e;
			}
			return "none";
		}

		public function catch_int64() {
			var big:Int64 = 9007199254740993;
			var held:* = big;
			var x:* = 5;
			var n:Number = 2.5;
			var w:Int64 = 7;
			trace(x is Int64, held is Int64, n is Int64, "5" is Int64, null is Int64, 4.0 is Int64, 1e300 is Int64, w is Int64, true is Int64, -0.0 is Int64);
			trace(x as Int64, held as Int64, n as Int64, "5" as Int64, (n as Int64) == null, (x as Int64) == 5);
			var back:Int64 = held as Int64;
			var none:Int64 = n as Int64;
			var o:Object = n as Int64;
			trace(back, none, o, back + 1);
			trace(thrown(big), thrown(3), thrown(2.5), thrown("text"), thrown(true), thrown(null));
			a: b: for (var i:int = 0; i < 2; i++) { continue a; }
		}
	}
}
