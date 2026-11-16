package {
	// Calls that nest deep through a function value, a toString() and an interface: each level is a call
	// like any other, whoever makes it, and only the depth of calls is limited.
	public class deep_values {
		private var chain:Link;

		public function deep_values() {
			var down:Function = null;
			down = function (n:int):int {
				return n == 0 ? 0 : 1 + down(n - 1);
			};
			trace(down(500));
			var sum:Function = function (n:Number, total:Number = 0):Number {
				return n == 0 ? total : sum(n - 1, total + n);
			};
			trace(sum(300), sum(3, 10));
			chain = null;
			for (var i:int = 0; i < 200; i++) {
				chain = new Link(i, chain);
			}
			trace(String(chain).length, chain.depth());
			var counter:Counted = chain;
			trace(counter.count(0));
			var method:Function = chain.depth;
			trace(method(), method.length, sum.length);
			try {
				down(100000);
			} catch (e:Error) {
				trace(e.message);
			}
		}
	}
}

interface Counted {
	function count(from:int):int;
}

class Link implements Counted {
	private var value:int;
	private var next:Link;

	public function Link(p_value:int, p_next:Link) {
		value = p_value;
		next = p_next;
	}

	public function toString():String {
		return next == null ? String(value) : value + "," + next;
	}

	public function depth():int {
		return next == null ? 1 : 1 + next.depth();
	}

	public function count(from:int):int {
		var after:Counted = next;
		return after == null ? from + 1 : after.count(from + 1);
	}
}
