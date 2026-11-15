package {
	// Objects that refer to each other in a loop, made in a function and left there. kas3c reports any that
	// are still alive at the end, so this passes only if the cycle collector frees them.
	public class cycles {
		public function cycles() {
			var sum:int = 0;
			for (var i:int = 0; i < 1000; i++) {
				sum += pair(i);
			}
			trace(sum);
			trace(ring(100));
			trace(factorials(10));
			trace(inVectors(50));
			var kept:Link = new Link(1);
			kept.next = new Link(2);
			kept.next.next = kept;
			trace(kept.next.next.value);
		}

		// Two that hold each other.
		private function pair(i:int):int {
			var a:Link = new Link(i);
			var b:Link = new Link(1);
			a.next = b;
			b.next = a;
			return b.next.value;
		}

		// A ring of n, walked once round.
		private function ring(n:int):int {
			var first:Link = new Link(0);
			var at:Link = first;
			for (var i:int = 1; i < n; i++) {
				at.next = new Link(i);
				at = at.next;
			}
			at.next = first;
			var sum:int = 0;
			at = first;
			do {
				sum += at.value;
				at = at.next;
			} while (at != first);
			return sum;
		}

		// A function expression that calls itself through the variable it is kept in: it and its
		// environment hold each other.
		private function factorials(n:int):String {
			var text:String = "";
			for (var i:int = 1; i <= n; i++) {
				var f:Function = function (k:int):Number { return k <= 1 ? 1 : k * f(k - 1); };
				text += f(i) + " ";
			}
			return text;
		}

		// Each link in a vector that the link holds, and a method of it held as a value.
		private function inVectors(n:int):int {
			var count:int = 0;
			for (var i:int = 0; i < n; i++) {
				var all:Vector.<Link> = new Vector.<Link>();
				var link:Link = new Link(i);
				link.all = all;
				link.method = link.twice;
				all.push(link);
				count += all.length + link.method();
			}
			return count;
		}
	}
}

class Link {
	public var value:int;
	public var next:Link;
	public var all:*;
	public var method:Function;

	public function Link(v:int) {
		value = v;
	}

	public function twice():int {
		return value * 2;
	}
}
