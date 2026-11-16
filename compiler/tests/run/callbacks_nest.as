package {
	// A library function's callback may call the library again as deep as calls may nest at all: what ends
	// it is the depth of calls (Error #1023 at the 10,001st), wherever the program runs.
	public class callbacks_nest {
		private var deepest:int = 0;
		private var calls:int = 0;
		public function callbacks_nest() {
			trace(sorted(300), deepest);
			try {
				endless();
			} catch (e:Error) {
				trace(e.errorID, calls);
			}
			// And afterwards everything is as it was.
			calls = 0;
			trace(sorted(70), counted([3, 1, 2]));
		}
		private function sorted(p_depth:int):int {
			if (p_depth > deepest) {
				deepest = p_depth;
			}
			var list:Array = [2, 1];
			list.sort(function (a:int, b:int):int {
				return p_depth == 0 ? a - b : sorted(p_depth - 1) * 0 + a - b;
			});
			return list[0];
		}
		private function endless():int {
			calls++;
			var list:Array = [2, 1];
			list.sort(function (a:int, b:int):int {
				return endless() + a - b;
			});
			return 0;
		}
		private function counted(p_list:Array):int {
			p_list.forEach(function (item:int, index:int, all:Array):void {
				calls += item;
			});
			return calls;
		}
	}
}
