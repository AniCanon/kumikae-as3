package {
	// An object where a Number is wanted, or beside `+`: its class's own valueOf(), else its text, as
	// ECMAScript's ToPrimitive has it. Checked against Node from a line-by-line copy.
	public class object_value_of {
		public function object_value_of() {
			var m:Money = new Money(150);
			trace(Number(m), m - 1, m < 2, m + 1, "x" + m, String(m), m * 2);
			var s:String = "s";
			s += m;
			var n:Number = 1;
			n += m;
			trace(s, n);
			var w:Words = new Words();
			trace(Number(w), w + 1, "x" + w, w * 2);
			var none:Words = null;
			trace(Number(none), "x" + none);
		}
	}
}

class Money {
	private var cents:int;

	public function Money(p_cents:int) {
		cents = p_cents;
	}

	public function valueOf():Number {
		return cents / 100;
	}

	public function toString():String {
		return "$" + cents / 100;
	}
}

class Words {
	public function toString():String {
		return "12";
	}
}
