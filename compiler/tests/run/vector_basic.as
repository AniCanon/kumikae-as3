package {
	public class vector_basic {
		private var kept:Vector.<int> = new Vector.<int>();
		private static var shared:Vector.<String> = new <String>["s"];

		public function vector_basic() {
			var ints:Vector.<int> = new Vector.<int>();
			trace(ints.length, ints.fixed, ints);
			ints[0] = 7;
			ints[1] = 8;
			ints[ints.length] = ints.length;
			trace(ints.length, ints[0], ints[1], ints[2], ints);
			ints[1] += 10;
			ints[2]++;
			trace(ints[1], ints[2], ints[0] * ints[2]);

			var sized:Vector.<int> = new Vector.<int>(3);
			var numbers:Vector.<Number> = new Vector.<Number>(2);
			var flags:Vector.<Boolean> = new Vector.<Boolean>(2);
			var words:Vector.<String> = new Vector.<String>(2);
			var counts:Vector.<uint> = new Vector.<uint>(1);
			trace(sized, numbers, flags, words, counts);
			numbers[0] = 1.5;
			numbers[1] = sized[0] + 2;
			flags[1] = true;
			words[0] = "a";
			counts[0] = -1;
			trace(numbers, flags, words, words[1] == null, counts[0]);

			var literal:Vector.<Number> = new <Number>[1, 2.5, -3];
			var empty:Vector.<String> = new <String>[];
			trace(literal, literal.length, empty.length, new <int>[4, 5][1]);

			var things:Vector.<Thing> = new Vector.<Thing>(1);
			trace(things[0] == null, things.length);
			things[0] = new Thing("one");
			things[1] = new Loud("two");
			trace(things[0].name, things[1].name, things[1] is Loud, things);

			var grid:Vector.<Vector.<int>> = new Vector.<Vector.<int>>();
			grid[0] = new <int>[1, 2];
			grid[1] = new <int>[3];
			grid[1][1] = 4;
			trace(grid.length, grid[0][1], grid[1], grid[1].length);

			sized.length = 5;
			trace(sized, sized.length);
			sized[4] = 9;
			sized.length = 2;
			sized.length += 1;
			trace(sized, sized.length);
			words.length = 1;
			words.length = 3;
			trace(words, things.length);
			things.length = 0;
			trace(things.length, things);

			var fixed:Vector.<int> = new Vector.<int>(2, true);
			fixed[1] = 5;
			trace(fixed.fixed, fixed, fixed.length);
			fixed.fixed = false;
			fixed[2] = 6;
			fixed.fixed = true;
			trace(fixed.fixed, fixed);

			var same:Vector.<int> = ints;
			same[0] = 100;
			trace(ints[0], same == ints, same != sized, ints is Vector.<int>);
			var none:Vector.<int>;
			trace(none == null, none, none is Vector.<int>);

			kept[0] = 1;
			shared[1] = "t";
			trace(kept, shared, kept.length + shared.length);
			trace(index(new <int>[10, 20, 30], 2), index(new <int>[10, 20, 30], 1.0));
		}

		private function index(v:Vector.<int>, at:Number):int {
			return v[at];
		}
	}
}

class Thing {
	public var name:String;
	public function Thing(name:String) {
		this.name = name;
	}
	public function toString():String {
		return "Thing(" + name + ")";
	}
}

class Loud extends Thing {
	public function Loud(name:String) {
		super(name + "!");
	}
}
