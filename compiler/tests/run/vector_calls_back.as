package { public class vector_calls_back {
	private var deepest:int = 0;
	public function vector_calls_back() {
		// A comparison that raises leaves the Array as it was, and its error is the program's to catch.
		var list:Array = [3, 1, 2];
		try {
			list.sort(function (a:int, b:int):int {
				throw new RangeError("no order");
			});
		} catch (e:RangeError) {
			trace(e.message, list);
		}
		// An element that can't say what it is: join() ends with its error.
		try {
			trace([1, new Silent(), 3].join("-"));
		} catch (e:Error) {
			trace("join:", e.message);
		}
		// sortOn() reads a getter, which may raise too.
		try {
			[new Named("b", false), new Named("a", true)].sortOn("name");
		} catch (e:Error) {
			trace("sortOn:", e.message);
		}
		var named:Array = [new Named("b", false), new Named("a", false)];
		named.sortOn("name");
		trace(named[0].name, named[1].name);
		// A getter is a property of its object, like a field: `in`, hasOwnProperty() and delete say so.
		var one:Object = named[0];
		trace("name" in one, one.hasOwnProperty("name"), delete one.name, "nothing" in one);
		// A sort whose comparison sorts, to a depth.
		trace(sorted(20), deepest);
		var numbers:Vector.<int> = new <int>[5, 4, 3];
		trace(numbers.sort(Array.NUMERIC), numbers.indexOf(4), Vector.<Number>(numbers).length);
		numbers.fixed = true;
		try {
			numbers.push(1);
		} catch (e:RangeError) {
			trace(e.errorID);
		}
		trace(numbers[7]);
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
} }
class Silent {
	public function toString():String {
		throw new Error("nothing to say");
	}
}
class Named {
	private var text:String;
	private var raises:Boolean;
	public function Named(p_text:String, p_raises:Boolean) {
		text = p_text;
		raises = p_raises;
	}
	public function get name():String {
		if (raises) {
			throw new Error("no name");
		}
		return text;
	}
}
