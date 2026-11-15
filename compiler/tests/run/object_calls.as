// A property called by its name takes the arguments as its function's parameters do: defaults filled in,
// the rest gathered.
package {
	public class object_calls {
		public function object_calls() {
			var o:Object = {};
			o.join = function (first:String, ...rest):String { return first + ":" + rest.join("+"); };
			o.greet = function (name:String = "you"):String { return "hi " + name; };
			trace(o.join("a", 1, 2, 3), o.join("b"), o.greet(), o.greet("bo"));
			var x:* = new Box();
			trace(x.sum(1, 2, 3), x.sum(), x.scaled(2), x.scaled(2, 10));
			try {
				o.greet("a", "b");
			} catch (e:ArgumentError) {
				trace(e.errorID);
			}
		}
	}
}

class Box {
	public function sum(...numbers):int {
		var total:int = 0;
		for each (var n:int in numbers) {
			total += n;
		}
		return total;
	}
	public function scaled(n:int, by:int = 3):int {
		return n * by;
	}
}
