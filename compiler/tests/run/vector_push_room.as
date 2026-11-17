package {
	// push() of one value, which native code writes where the vector has room and leaves to the library
	// where it has none, is fixed or is missing: every kind of element, the length it gives, a vector that
	// holds itself, and what a vector holds let go of once (the runner counts what is left alive).
	public class vector_push_room {
		private var kept:Vector.<Item> = new Vector.<Item>();

		public function vector_push_room() {
			var ints:Vector.<int> = new Vector.<int>();
			var last:uint = 0;
			for (var i:int = 0; i < 40; i++) {
				last = ints.push(i * 3);
			}
			trace(last, ints.length, ints[0], ints[17], ints[39]);
			var numbers:Vector.<Number> = new <Number>[0.5];
			var flags:Vector.<Boolean> = new Vector.<Boolean>();
			var words:Vector.<String> = new Vector.<String>();
			var wide:Vector.<Int64> = new Vector.<Int64>();
			var word:String = "w";
			for (i = 0; i < 9; i++) {
				numbers.push(i / 4);
				flags.push(i % 3 == 0);
				word += i;
				words.push(word);
				words.push(null);
				wide.push(Int64(i) * 4000000000);
			}
			trace(numbers, flags.length, flags[3], flags[4], words.length, words[16], words[17], wide[8]);
			for (i = 0; i < 7; i++) {
				var item:Item = new Item(i);
				trace(kept.push(item), item.id);
				kept.push(null);
			}
			trace(kept.length, kept[12].id, kept[13]);
			var list:Array = [];
			for (i = 0; i < 12; i++) {
				list.push(i % 2 ? "s" + i : i);
			}
			list.push(list.length, undefined, null);
			list.push(kept[0]);
			trace(list.length, list);
			var holes:Array = new Array(3);
			trace(holes.push("end"), holes);
			var nested:Array = [];
			nested.push(nested.length);
			nested.push([nested[0]]);
			trace(nested);
			var grown:Vector.<int> = new Vector.<int>(3);
			grown.length = 1;
			trace(grown.push(7), grown.push(8), grown.push(9), grown);
			var fixed:Vector.<String> = new Vector.<String>(2, true);
			try {
				fixed.push("no");
			} catch (e:RangeError) {
				trace(e.message, fixed.length);
			}
			fixed.fixed = false;
			trace(fixed.push("yes"), fixed);
			var none:Vector.<Item> = null;
			try {
				none.push(kept[0]);
			} catch (f:TypeError) {
				trace(f.message);
			}
			kept.length = 3;
			trace(kept.push(kept[0]), kept[3].id);
		}
	}
}

class Item {
	public var id:int;
	public function Item(id:int) {
		this.id = id;
	}
	public function toString():String {
		return "Item" + id;
	}
}
