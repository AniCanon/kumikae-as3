package {
	// Objects, Strings and untyped values handed from where they are made to a variable or a field: in a
	// loop, where each round's takes the place of the last; with an error between the making and the
	// handing over; and a variable read again after it was copied. Native code hands over the reference
	// where the copy is the last use; nothing may be let go of twice or kept for ever (the runner counts).
	public class last_use {
		private var kept:Item;
		private var label:String;
		private var any:*;
		private static var shared:Item;

		public function last_use() {
			var total:int = 0;
			for (var i:int = 0; i < 5; i++) {
				var item:Item = new Item(i);
				kept = new Item(i * 2);
				label = "n" + i;
				any = [i, i + 1];
				shared = new Item(i * 3);
				total += item.value + kept.value + label.length + any.length + shared.value;
			}
			trace(total, kept.value, label, any, shared.value);
			try {
				var bad:Item = new Item(-1);
				trace(bad.value);
			} catch (e:Error) {
				trace(e.message, kept.value);
			}
			var same:Item = kept;
			var again:Item = same;
			trace(again === kept, same === again, again.value);
			var text:String = label + "!";
			var copy:String = text;
			trace(text, copy);
			trace(pick(true).value, pick(false).value);
		}

		private function pick(first:Boolean):Item {
			var made:Item = first ? new Item(1) : new Item(2);
			var other:Item = made;
			if (first) {
				other = new Item(10);
			}
			return other;
		}
	}
}

class Item {
	public var value:int;

	public function Item(given:int) {
		if (given < 0) {
			throw new Error("negative");
		}
		value = given;
	}
}
