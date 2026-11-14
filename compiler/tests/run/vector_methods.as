package {
	public class vector_methods {
		public function vector_methods() {
			var v:Vector.<int> = new <int>[3, 1, 2];
			trace(v.push(4), v.push(5, 6), v.push(), v);
			trace(v.pop(), v.shift(), v, v.length);
			trace(v.unshift(9), v.unshift(7, 8), v);
			trace(v.indexOf(2), v.indexOf(9), v.indexOf(42), v.indexOf(8, 2), v.indexOf(4, -1), v.indexOf(4, -20));
			v.push(1);
			trace(v.lastIndexOf(1), v.lastIndexOf(1, 5), v.lastIndexOf(1, -2), v.lastIndexOf(7, 0), v.lastIndexOf(8, -100));
			trace(v.reverse(), v.reverse() == v, v);
			trace(v.slice(), v.slice(2), v.slice(1, 3), v.slice(-2), v.slice(1, -1), v.slice(4, 2), v.slice(-100, 100));
			trace(v.slice() == v, v.concat(), v.concat(new <int>[0]), v.concat(new <int>[0, 0], new <int>[5]).length, v.length);
			trace(v.join(), v.join("-"), v.join(""), v.toString(), new <int>[].join("x") == "");
			v.insertAt(1, 100);
			v.insertAt(-1, 200);
			v.insertAt(99, 300);
			trace(v, v.removeAt(0), v.removeAt(-1), v.removeAt(1), v);
			trace(v.splice(1, 2), v);
			trace(v.splice(1, 0, 11, 12), v);
			trace(v.splice(-2, 1, 13), v);
			trace(v.splice(2), v, v.splice(0, 1).length, v);
			var w:Vector.<String> = new <String>["b", "a", null];
			trace(w.indexOf("a"), w.indexOf(null), w.indexOf("z"), w.join("+"), w.pop() == null, w.pop(), w.shift(), w.length);
			var n:Vector.<Number> = new <Number>[1.5, NaN, 2];
			trace(n.indexOf(2), n.indexOf(NaN), n.lastIndexOf(1.5), n.push(0.25), n);
			var emptyInts:Vector.<int> = new Vector.<int>();
			var emptyNumbers:Vector.<Number> = new Vector.<Number>();
			var emptyWords:Vector.<String> = new Vector.<String>();
			trace(emptyInts.pop(), emptyNumbers.pop(), emptyWords.shift(), emptyInts.length);
			var things:Vector.<Item> = new Vector.<Item>();
			var first:Item = new Item("first");
			things.push(first, new Item("second"), first);
			trace(things.indexOf(first), things.lastIndexOf(first), things.indexOf(new Item("first")), things.join("|"));
			var copy:Vector.<Item> = things.slice(1);
			things.length = 0;
			trace(copy, copy[1] == first, copy.removeAt(0), copy);
			var self:Vector.<int> = new <int>[1, 2];
			self.push(self.length, self[0]);
			self[self.push(7) - 1] = self.pop() + 1;
			trace(self);
			var fixed:Vector.<int> = new <int>[1, 2, 3];
			fixed.fixed = true;
			trace(fixed.splice(0, 1, 9), fixed.reverse(), fixed.indexOf(9), fixed.slice(1).fixed);
			var total:int = 0;
			for each (var x:int in new <int>[5, 6, 7]) {
				total += x;
			}
			var seen:String = "";
			for (var i:int in new <String>["a", "b"]) {
				seen += i;
			}
			var grow:Vector.<int> = new <int>[1, 2];
			var item:int;
			for each (item in grow) {
				if (item < 3) {
					grow.push(item + 2);
				}
				if (item == 2) {
					continue;
				}
				seen += item;
				if (item == 4) {
					break;
				}
			}
			var missing:Vector.<int>;
			for each (var never:int in missing) {
				seen += "!";
			}
			trace(total, seen, grow, item);
		}
	}
}

class Item {
	private var name:String;
	public function Item(name:String) {
		this.name = name;
	}
	public function toString():String {
		return name;
	}
}
