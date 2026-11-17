package {
	// An object read out of a vector into a variable, in functions that let go of nothing (native code then
	// counts no reference for the variable: the vector holds the object all the while) and in functions that
	// do, where it counts none only if every use follows the read at once, and one otherwise. Every way out
	// is tried: the end, a return of the element itself, a null element, an index past the end, an error
	// caught. Nothing may be let go of twice or kept for ever (the runner counts).
	public class element_lent {
		private var cells:Vector.<Cell> = new Vector.<Cell>();
		private var small:Vector.<Small> = new Vector.<Small>();
		private var none:Vector.<Cell>;
		private var tags:Vector.<Tag> = new Vector.<Tag>();

		public function element_lent() {
			for (var i:int = 0; i < 6; i++) {
				cells.push(new Cell(i, "c" + i));
				small.push(new Small(i * 0.5));
			}
			trace(sum(), pairs(), sumSmall());
			trace(pick(2).name, pick(5).name, picked(4));
			cells[3] = null;
			try {
				trace(sum());
			} catch (e:Error) {
				trace("null:", e.message);
			}
			try {
				trace(pick(9).name);
			} catch (e:Error) {
				trace("past:", e.message);
			}
			cells[3] = new Cell(30, "again");
			trace(replace(1), sum(), last().name);
			try {
				trace(bump(8));
			} catch (e:Error) {
				trace("bump:", e.message, sum());
			}
			try {
				trace(sumNone());
			} catch (e:Error) {
				trace("none:", e.message);
			}
			trace(refill(3), sum(), caught());
			var kept:Cell = pick(0);
			cells.length = 0;
			trace(kept.name, kept.value, cells.length);
			small.length = 2;
			trace(sumSmall());
			tags.push(new Tag("t0", 1));
			tags.push(new Tag("t1", 2));
			trace(copied(), copiedTag(), small[0].x, tags[0].name);
			cells.push(new Cell(1, "a"));
			cells.push(new Cell(2, "b"));
			cells.push(new Cell(3, "c"));
			trace(outlives(), cells.length);
		}

		// Lets go of nothing: `c` and `d` count nothing.
		private function sum():int {
			var total:int = 0;
			var n:int = cells.length;
			for (var i:int = 0; i < n; i++) {
				var c:Cell = cells[i];
				total += c.value;
				c.value += 1;
			}
			return total;
		}

		private function pairs():int {
			var total:int = 0;
			var n:int = cells.length;
			for (var i:int = 0; i < n; i++) {
				var c:Cell = cells[i];
				for (var j:int = i + 1; j < n; j++) {
					var d:Cell = cells[j];
					var e:Cell = d;
					total += c.value * e.value;
				}
			}
			return total;
		}

		private function sumSmall():Number {
			var total:Number = 0;
			for (var i:int = 0; i < small.length; i++) {
				var s:Small = small[i];
				total += s.x;
			}
			return total;
		}

		// Past the vector's end part of the way through a loop: what the loop did before the error stays done.
		private function bump(times:int):int {
			var total:int = 0;
			for (var i:int = 0; i < times; i++) {
				var c:Cell = cells[i];
				c.value += 10;
				total += c.value;
			}
			return total;
		}

		// A loop over a vector that is not there.
		private function sumNone():int {
			var total:int = 0;
			for (var i:int = 0; i < 3; i++) {
				var c:Cell = none[i];
				total += c.value;
			}
			return total;
		}

		// Lets go of things (each new object takes the place of one), and reads each element only to use it
		// at once: `c` counts nothing.
		private function refill(times:int):int {
			var total:int = 0;
			for (var round:int = 0; round < times; round++) {
				var i:int;
				for (i = 0; i < cells.length; i++) {
					cells[i] = new Cell(i + round, "r" + i);
				}
				for (i = 0; i < cells.length; i++) {
					var c:Cell = cells[i];
					total += c.value;
				}
			}
			return total;
		}

		// Read at once in a loop that an error leaves, and read again where the error is caught.
		private function caught():String {
			var c:Cell = null;
			var seen:int = 0;
			try {
				for (var i:int = 0; i < 10; i++) {
					c = cells[i];
					seen += c.value;
				}
			} catch (e:Error) {
				return "caught " + c.name + " " + seen;
			}
			return "none";
		}

		// The object is taken out of the vector, and the vector let go of, between the read and the use: the
		// variables keep their objects.
		private function outlives():String {
			var c:Cell = cells[1];
			var d:Cell = cells[2];
			cells[1] = null;
			var text:String = c.name;
			cells = new Vector.<Cell>();
			return text + d.name + c.value;
		}

		// A copy of a variable that counts a reference (its object has left the vector), which then lets go
		// of it: the copy counts one of its own, and still has the object when another is made.
		private function copied():Number {
			var b:Small = small[0];
			small[0] = new Small(-1);
			var a:Small = b;
			b = small[1];
			var c:Small = new Small(99);
			return a.x + c.x + b.x;
		}

		// The same with objects that hold a String, which are given back to the heap when let go of.
		private function copiedTag():String {
			var b:Tag = tags[0];
			tags[0] = new Tag("new", -1);
			var a:Tag = b;
			b = tags[1];
			var c:Tag = new Tag("made", 99);
			return a.name + a.x + c.name + b.name;
		}

		// The element itself is the result: the caller's reference is one more.
		private function pick(at:int):Cell {
			var c:Cell = cells[at];
			return c;
		}

		private function picked(at:int):String {
			var c:Cell = pick(at);
			var d:Cell = pick(at);
			return c.name + (c === d);
		}

		private function last():Cell {
			var found:Cell = null;
			for (var i:int = 0; i < cells.length; i++) {
				found = cells[i];
			}
			return found;
		}

		// Takes the object out of the vector while a variable still has it: the variable keeps it alive.
		private function replace(at:int):String {
			var c:Cell = cells[at];
			cells[at] = new Cell(100, "new");
			var d:Cell = cells[at];
			return c.name + c.value + d.name + d.value;
		}
	}
}

class Cell {
	public var value:int;
	public var name:String;

	public function Cell(value:int, name:String) {
		this.value = value;
		this.name = name;
	}
}

class Small {
	public var x:Number;

	public function Small(x:Number) {
		this.x = x;
	}
}

class Tag {
	public var name:String;
	public var x:Number;

	public function Tag(name:String, x:Number) {
		this.name = name;
		this.x = x;
	}
}
