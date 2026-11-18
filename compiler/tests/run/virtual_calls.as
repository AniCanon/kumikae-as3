package {
	// Calls through a class's method table, where native code knows every class the receiver may be of (the
	// program is whole): it then calls the class's own method after comparing classes, or with no
	// comparison where one method serves them all, and takes the call to do no more than every override
	// does. Each case here is one an override would break if it were not looked at: one that takes the
	// receiver out of the vector it was read from, one that makes that vector shorter, one that makes a
	// vector of numbers longer, one that writes a field the caller keeps, one that throws, one that calls
	// a function value. The receiver is also untyped, of an interface, null, and of a class with too many
	// subclasses to compare.
	public class virtual_calls {
		public static var main:virtual_calls;
		public static var callback:Function;

		public var items:Vector.<Item> = new Vector.<Item>();
		public var counts:Vector.<int> = new Vector.<int>();
		public var spare:Item;
		public var hits:int = 0;
		private var shapes:Vector.<Shape> = new Vector.<Shape>();
		private var many:Vector.<Many> = new Vector.<Many>();
		private var leaf:Item = new Item(1, 0);

		public function virtual_calls() {
			main = this;
			shapes.push(new Circle(1), new Rect(2, 3), new Tri(4, 5), new Shape(), new Square(2));
			trace(areas(), areas());
			many.push(new Many(), new Many1(), new Many2(), new Many3(), new Many4(), new Many5());
			trace(weights(), weights());

			fill();
			items[1] = new Leaver(7, 1);
			trace("leaves", touched(), items[1].value(), spare.value());
			fill();
			callback = function():int { items[2] = new Item(50, 2); spare = new Item(998, 0); return 3; };
			items[2] = new Caller(9, 2);
			trace("called", touched(), items[2].value(), spare.value());
			fill();
			items[0] = new Shrinker(4, 0);
			try {
				trace(touched());
			} catch (e:Error) {
				trace("shorter:", e.message, items.length);
			}
			fill();
			items[1] = new Grower(2, 1);
			counts.push(1);
			trace("longer", counted(), counts.length, counts[0]);
			fill();
			items[2] = new Thrower(3, 2);
			trace("throws", caught());
			fill();
			items[3] = new Hitter(5, 3);
			trace("field", struck(), hits);
			hits = 0;
			trace("between", between(new Peeker(0, 0)), between(new Hitter(0, 0)), between(new Item(0, 0)));
			fill();
			trace("results", selves());

			var untyped:* = shapes[0];
			untyped.grow(1);
			var measured:Measured = shapes[1];
			var square:Square = new Square(3);
			square.grow(1);
			trace(untyped.area() > 12, measured.area(), square.area(), new Solo().twice(4), new Runner().run(), new Sprinter().run());
			var none:Shape = null;
			try {
				none.grow(1);
			} catch (e:Error) {
				trace("none:", e.errorID);
			}
			for (var depth:int = 9997; depth <= 10001; depth++) {
				try {
					trace(depth, down(depth));
				} catch (e:Error) {
					trace(depth, e.errorID);
				}
			}
		}

		private function fill():void {
			items.length = 0;
			for (var i:int = 0; i < 4; i++) {
				items.push(new Item(i + 1, i));
			}
		}

		// Every override is sums on the object's own fields.
		private function areas():Number {
			var sum:Number = 0;
			var n:int = shapes.length;
			for (var i:int = 0; i < n; i++) {
				var s:Shape = shapes[i];
				s.grow(0.5);
				sum += s.area();
			}
			return sum;
		}

		// Too many classes to compare: the method table.
		private function weights():int {
			var sum:int = 0;
			for (var i:int = 0; i < many.length; i++) {
				var m:Many = many[i];
				sum += m.weight() * (i + 1);
			}
			return sum;
		}

		// An override may take the receiver out of the vector: it is still used after the call.
		private function touched():int {
			var total:int = 0;
			var n:int = items.length;
			for (var i:int = 0; i < n; i++) {
				var it:Item = items[i];
				total += it.touch();
				total += it.value() * 10;
			}
			return total;
		}

		// An override may make a vector of numbers longer, and write its first element, while a loop reads it.
		private function counted():int {
			var total:int = 0;
			for (var i:int = 0; i < 40; i++) {
				var it:Item = items[i % 4];
				total += counts[0];
				total += it.fill(counts);
			}
			return total;
		}

		private function caught():String {
			var said:String = "";
			for (var i:int = 0; i < items.length; i++) {
				var it:Item = items[i];
				try {
					said += it.touch();
				} catch (e:Error) {
					said += "[" + e.message + "]";
				}
			}
			return said;
		}

		// An override may write the field this loop reads.
		private function struck():int {
			var total:int = 0;
			for (var i:int = 0; i < items.length; i++) {
				var it:Item = items[i];
				total += it.strike(this);
				total += hits;
			}
			return total;
		}

		// The field is in a local for the loops: an override called between them may read it, or write it.
		private function between(it:Item):int {
			for (var i:int = 0; i < 3; i++) {
				hits += 2;
			}
			var seen:int = it.strike(this);
			for (i = 0; i < 3; i++) {
				hits += 1;
			}
			return seen * 1000 + hits;
		}

		private function selves():int {
			var total:int = 0;
			for (var i:int = 0; i < items.length; i++) {
				var it:Item = items[i];
				var same:Item = it.self();
				var other:Item = it.next();
				it.next();
				total += same.value() + other.value();
			}
			return total;
		}

		private function down(left:int):int {
			if (left == 0) {
				return leaf.touch();
			}
			return down(left - 1) + 1;
		}
	}
}

interface Measured {
	function area():Number;
}

class Shape implements Measured {
	public function area():Number { return 0; }
	public function grow(d:Number):void { }
}

final class Circle extends Shape {
	private var r:Number;
	public function Circle(r:Number) { this.r = r; }
	override public function area():Number { return 3 * r * r; }
	override public function grow(d:Number):void { r += d; }
}

class Rect extends Shape {
	protected var w:Number;
	protected var h:Number;
	public function Rect(w:Number, h:Number) { this.w = w; this.h = h; }
	override public function area():Number { return w * h; }
	override public function grow(d:Number):void { w += d; h += d; }
}

// Takes its area from Rect, and grows its own way.
class Square extends Rect {
	public function Square(side:Number) { super(side, side); }
	override public function grow(d:Number):void { w += d * 2; h += d * 2; }
}

final class Tri extends Shape {
	private var b:Number;
	private var h:Number;
	public function Tri(b:Number, h:Number) { this.b = b; this.h = h; }
	override public function area():Number { return 0.5 * b * h; }
	override public function grow(d:Number):void { b += d; }
}

class Many { public function weight():int { return 1; } }
class Many1 extends Many { override public function weight():int { return 2; } }
class Many2 extends Many { override public function weight():int { return 3; } }
class Many3 extends Many { override public function weight():int { return 4; } }
class Many4 extends Many { override public function weight():int { return 5; } }
class Many5 extends Many4 { override public function weight():int { return 6; } }

class Item {
	protected var v:int;
	protected var at:int;
	public function Item(v:int, at:int) { this.v = v; this.at = at; }
	public function value():int { return v; }
	public function touch():int { return 1; }
	public function fill(into:Vector.<int>):int { return 1; }
	public function strike(on:virtual_calls):int { return 1; }
	public function self():Item { return this; }
	public function next():Item { return new Item(v + 100, at); }
}

// Puts another in its place, and makes one more: what is let go of there must not be this one's memory.
class Leaver extends Item {
	public function Leaver(v:int, at:int) { super(v, at); }
	override public function touch():int {
		virtual_calls.main.items[at] = new Item(40, at);
		virtual_calls.main.spare = new Item(999, 0);
		return 2;
	}
}

class Caller extends Item {
	public function Caller(v:int, at:int) { super(v, at); }
	override public function touch():int { return virtual_calls.callback(); }
}

class Shrinker extends Item {
	public function Shrinker(v:int, at:int) { super(v, at); }
	override public function touch():int {
		virtual_calls.main.items.length = 1;
		return 4;
	}
}

class Grower extends Item {
	public function Grower(v:int, at:int) { super(v, at); }
	override public function fill(into:Vector.<int>):int {
		into[0] = into[0] + 100;
		into[into.length] = 5;
		return 2;
	}
}

class Thrower extends Item {
	public function Thrower(v:int, at:int) { super(v, at); }
	override public function touch():int { throw new Error("no " + v); }
}

class Hitter extends Item {
	public function Hitter(v:int, at:int) { super(v, at); }
	override public function strike(on:virtual_calls):int {
		on.hits += 10;
		return 1;
	}
}

class Peeker extends Item {
	public function Peeker(v:int, at:int) { super(v, at); }
	override public function strike(on:virtual_calls):int { return on.hits; }
}

final class Solo {
	public function twice(x:int):int { return x * 2; }
}

class Runner {
	public function run():int { return step() * 10 + 1; }
	public function step():int { return 1; }
}

class Sprinter extends Runner {
	override public function step():int { return 7; }
}
