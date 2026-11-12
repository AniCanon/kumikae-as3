package {
	import game.Counter;
	import game.util.double;
	public class Main {
		public function Main() {
			var counter:Counter = new Counter(3);
			counter.add(double(4));
			trace(counter.total, Counter.made, game.util.double(10), limit);
			limit = limit + 1;
			trace(limit, helper());
		}
	}
}
var limit:int = 99;
function helper():String { return "helped " + limit; }
