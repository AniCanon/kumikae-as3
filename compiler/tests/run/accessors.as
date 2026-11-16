package {
	// Getters and setters: each read calls the getter once and each write the setter once, in AS3's order.
	public class accessors {
		public function accessors() {
			trace(Counter.made, Counter.doubled); // Read before any Counter is made: the class is set up first.
			Counter.made = 3;
			trace(Counter.made, Counter.doubled);
			var c:Counter = new Counter();
			trace(c.value);
			c.value = 5;
			trace(c.value);
			c.value += 2;
			c.value++;
			++c.value;
			var old:int = c.value--;
			trace(old, c.value, c.reads, c.writes);
			pick(c).value = note("value", 10);
			pick(c).value += note("step", 1);
			pick(c).value *= note("times", 2);
			trace(c.value, c.reads, c.writes);
			var assigned:int = (c.value = 42);
			trace(assigned, c.label);
			c.bump();
			trace(c.value);
			c.flag ||= true;
			c.flag ||= false;
			c.flag &&= false;
			trace(c.flag);
			trace(c.secretOf(new Counter()), c.action(3));
			var d:Counter = new Doubler();
			d.value = 4;
			trace(d.value);
			d.value += 1;
			trace(d.value, d.label);
			var p:Plain = new Plain();
			p.value = 7;
			p.value++;
			trace(p.value, p.label);
			var h:Half = new Half();
			h.value = 6;
			trace(h.value);
		}
		private function pick(p_counter:Counter):Counter {
			trace("pick");
			return p_counter;
		}
		private function note(p_what:String, p_value:int):int {
			trace(p_what);
			return p_value;
		}
	}
}

class Counter {
	private var n:int = 0;
	public var reads:int = 0;
	public var writes:int = 0;
	private static var count:int = 7;
	private var on:Boolean = false;
	public function get value():int {
		reads++;
		return n;
	}
	public function set value(p_value:int):void {
		writes++;
		n = p_value;
	}
	public static function get made():int { return count; }
	public static function set made(p_value:int):void { count = p_value; }
	public static function get doubled():int { return made * 2; }
	public function get label():String { return "counter " + value; }
	private function get secret():int { return 99; }
	public function secretOf(p_other:Counter):int { return p_other.secret; }
	public function get action():Function { return function(k:int):int { return k * 10; }; }
	public function bump():void { value = value + 1; }
	public function get flag():Boolean { return on; }
	public function set flag(p_value:Boolean):void {
		trace("flag", p_value);
		on = p_value;
	}
}

// Overrides reach the base's halves with super.
class Doubler extends Counter {
	override public function get value():int { return super.value * 2; }
	override public function set value(p_value:int):void { super.value = p_value + 100; }
}

// Inherits both halves.
class Plain extends Counter {}

// Overrides one half and inherits the other.
class Half extends Counter {
	override public function get value():int { return super.value + 1000; }
}
