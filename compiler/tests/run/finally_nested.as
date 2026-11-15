package {
	public class finally_nested {
		public function finally_nested() {
			trace(twoDeep());
			outer: for (var i:int = 0; i < 3; i++) {
				try {
					try {
						if (i == 1) { continue outer; }
						if (i == 2) { break outer; }
					} finally { trace("inner", i); }
				} finally { trace("outer", i); }
				trace("after", i);
			}
			var big:* = 5000000000;
			var sum:* = big + 1;
			trace(sum, typeof sum, sum - 1, big * 2, 9223372036854775807 + 0);
			try { calls(); } catch (e:Error) { trace("caught", e.message); }
		}
		private function twoDeep():String {
			var log:String = "";
			try {
				try { return "value"; } finally { log += "a"; }
			} finally { log += "b"; trace(log); }
			return "never";
		}
		private function calls():void {
			try { thrower(); } finally { trace("callee finally"); }
		}
		private function thrower():void { throw new Error("deep"); }
	}
}
