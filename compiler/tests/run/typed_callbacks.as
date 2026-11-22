package {
	public class typed_callbacks {
		private var total:int = 4;
		private function add(x:int):int { return total += x; }
		private function adder(start:int):(int)=>int {
			return function(x:int):int { return start += x; };
		}
		private function invoke(f:(int)=>int, x:int):int { return f(x); }
		private static function describe(x:String, y:int):String {
			return x + "/" + y + "/" + arguments.length;
		}
		public function typed_callbacks() {
			var f:(int)=>int = adder(10);
			trace(invoke(f, 2), f(3));
			f = add;
			trace(f(5), invoke(f, 1));
			var erased:Function = f;
			trace(erased(3));
			var describeIt:(String,int)=>String = describe;
			trace(describeIt("ok", 7));
			var say:()=>void = function():void { trace("void"); };
			say();
			var through:Function = invoke;
			try { through(describeIt, 2); } catch (mismatch:TypeError) { trace(mismatch.message); }
			var missing:()=>void = null;
			try { missing(); } catch (empty:TypeError) { trace("null"); }
		}
	}
}
