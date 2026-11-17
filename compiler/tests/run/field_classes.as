package {
	// One object reached through two variables, by every way native code learns a variable's class (a
	// parameter, a field, a vector's element, a new object, a copy, `as`) and by ways it learns nothing (a
	// call's result, an untyped value). Native code tells the C++ compiler which field of which class each
	// read and write is of, so that a write of one field leaves a read of another alone: a write through
	// one variable must still be seen through the other when the two are one object.
	public class field_classes {
		private var held:Pt = new Pt(1, 2);
		private var pts:Vector.<Pt> = new Vector.<Pt>();
		private var x:Number = 7;

		public function field_classes() {
			var p:Pt = new Pt(3, 4);
			trace("two:", two(p, p), two(p, new Pt(10, 20)));
			trace("self:", p.mix(p), p.x, p.y, p.mix(held), held.x);
			var d:Deep = new Deep(1, 2, 3);
			trace("base:", based(d, d), d.x, d.y, d.z);
			held = p;
			trace("field:", throughField(p), p.x, p.y);
			pts.push(p);
			pts.push(d);
			pts.push(p);
			trace("pairs:", pairs(0.5), p.x, p.y, d.x, d.y);
			trace("loop:", loop(p, p, 5), loop(p, d, 5), p.x, d.x);
			trace("unknown:", unknown(p), p.x, untyped(p, p), p.y);
			trace("as:", cast(d, d), d.z, d.x);
			var o:Other = new Other(9);
			trace("other:", apart(p, o), p.x, o.x);
			trace("own:", own(this), x);
		}

		private function two(a:Pt, b:Pt):Number {
			a.x = 1;
			b.x += 2;
			a.y = b.x + a.x;
			b.y *= 2;
			return a.x + a.y + b.x + b.y;
		}

		private function based(d:Deep, p:Pt):Number {
			d.x = 1;
			p.x = 2;
			d.z = p.x + d.x;
			p.y = d.z;
			d.y += 1;
			return d.x + p.y + d.z;
		}

		private function throughField(p:Pt):Number {
			p.x = 10;
			held.x += 1;
			p.y = held.x + p.x;
			held.y -= 2;
			return p.x + p.y;
		}

		// As the n-body benchmark's loop: every pair of elements, each changing the other. One object is in
		// the vector twice, so one pair is an object with itself.
		private function pairs(dt:Number):Number {
			var n:int = pts.length;
			for (var i:int = 0; i < n; i++) {
				var a:Pt = pts[i];
				for (var j:int = i + 1; j < n; j++) {
					var b:Pt = pts[j];
					var dx:Number = a.x - b.x + 1;
					a.y -= dx * b.x * dt;
					b.y += dx * a.x * dt;
					a.x += b.y;
					b.x -= a.y * dt;
				}
			}
			var sum:Number = 0;
			for (i = 0; i < n; i++) {
				var c:Pt = pts[i];
				c.x += dt * c.y;
				sum += c.x;
			}
			return sum;
		}

		// A field read in a loop that another variable writes in the same loop.
		private function loop(a:Pt, b:Pt, times:int):Number {
			var total:Number = 0;
			for (var i:int = 0; i < times; i++) {
				total += a.x;
				b.x += 1;
				total += a.y;
			}
			return total;
		}

		private function same(p:Pt):Pt {
			return p;
		}

		private function unknown(p:Pt):Number {
			var q:Pt = same(p);
			p.x = 1;
			q.x = 2;
			p.y = p.x + q.x;
			return p.y;
		}

		private function untyped(p:Pt, any:*):Number {
			var q:Pt = any;
			p.y = 5;
			q.y += 1;
			return p.y + q.y;
		}

		private function cast(p:Pt, d:Deep):Number {
			var e:Deep = p as Deep;
			e.z = 1;
			d.z += 4;
			e.x = d.z;
			return e.z + p.x;
		}

		// Two classes with a Number in the same place: never one object.
		private function apart(p:Pt, o:Other):Number {
			p.x = 1;
			o.x = 2;
			p.x += o.x;
			return p.x + o.x;
		}

		private function own(me:field_classes):Number {
			x = 1;
			me.x += 2;
			x *= 5;
			return me.x + x;
		}
	}
}

class Pt {
	public var x:Number, y:Number;

	public function Pt(x:Number, y:Number) {
		this.x = x;
		this.y = y;
	}

	public function mix(other:Pt):Number {
		x = 1;
		other.x = 5;
		y = x + other.x;
		other.y += x;
		return x + y;
	}
}

class Deep extends Pt {
	public var z:Number;

	public function Deep(x:Number, y:Number, z:Number) {
		super(x, y);
		this.z = z;
	}
}

class Other {
	public var x:Number;

	public function Other(x:Number) {
		this.x = x;
	}
}
