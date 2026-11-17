package {
	// Objects, Strings and untyped values handed from where they are made to a variable, a field or a
	// vector's place: in a loop, where each round's takes the place of the last; with an error between the
	// making and the handing over; a variable read again after it was copied; a place past the vector's end
	// and one in a fixed vector, which the library is left. Native code hands over the reference
	// where the copy is the last use; nothing may be let go of twice or kept for ever (the runner counts).
	public class last_use {
		private var kept:Item;
		private var label:String;
		private var any:*;
		private static var shared:Item;
		private var items:Vector.<Item> = new Vector.<Item>();
		private var texts:Vector.<String> = new Vector.<String>(2, true);
		private var mixed:Array = [];
		public var slies:Vector.<Sly> = new Vector.<Sly>(1);

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
			trace(places(4), items.length, items[0].value, items[3].value, texts, mixed);
			try {
				texts[2] = "t" + items.length;
			} catch (e:Error) {
				trace(e.message, texts);
			}
			try {
				items[1] = new Item(-5);
			} catch (e:Error) {
				trace(e.message, items[1].value);
			}
			try {
				items[9] = new Item(9);
			} catch (e:Error) {
				trace(e.message, items.length);
			}
			trace(sly(), slies.length, slies[0], slies[1]);
		}

		// The constructor puts another vector in the field the store's vector was read from: the object goes
		// into the vector that was there before, which goes with it.
		private function sly():int {
			slies[0] = new Sly(this, 7);
			return slies[0] == null ? -1 : slies[0].n;
		}

		// Each round's object takes the place of the last round's, and is added where the vector ends; one
		// stored and then read again keeps its own reference.
		private function places(rounds:int):int {
			var total:int = 0;
			for (var round:int = 0; round < rounds; round++) {
				for (var i:int = 0; i < 4; i++) {
					items[i] = new Item(round * 10 + i);
					texts[i % 2] = "t" + round + i;
					mixed[i] = new Item(i);
				}
				var again:Item = new Item(round);
				items[2] = again;
				total += again.value + items[2].value + items[3].value + (mixed[1] as Item).value;
			}
			return total;
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

class Sly {
	public var n:int;

	public function Sly(owner:last_use, n:int) {
		owner.slies = new Vector.<Sly>(2);
		this.n = n;
	}
}
