package {
	// A vector that is changed while it is being put into words: by an element's own toString().
	public class vector_reentry {
		public static var outer:Vector.<Vector.<Noisy>>;

		public function vector_reentry() {
			outer = new Vector.<Vector.<Noisy>>();
			fill(1);
			trace(outer);
			trace(outer.length);
			fill(2);
			trace(outer.join("|"), outer.length);
			var numbers:Vector.<Number> = new Vector.<Number>();
			var whole:Vector.<int> = new Vector.<int>();
			trace(numbers.pop(), numbers.shift(), whole.pop(), numbers.length);
		}

		// In a function of its own: nothing but the outer vector holds the inner ones once it returns.
		private function fill(count:int):void {
			for (var i:int = 0; i < count; i++) {
				outer.push(make());
			}
		}

		private function make():Vector.<Noisy> {
			var inner:Vector.<Noisy> = new Vector.<Noisy>();
			inner.push(new Noisy("x"), new Noisy("y"), new Noisy("z"));
			return inner;
		}
	}
}

class Noisy {
	public var n:String;
	public function Noisy(n:String) {
		this.n = n;
	}
	// Empties the vector it is being printed from, and makes another to take the freed memory.
	public function toString():String {
		vector_reentry.outer.length = 0;
		var other:Vector.<Noisy> = new Vector.<Noisy>(8);
		return n + n + other.length;
	}
}
