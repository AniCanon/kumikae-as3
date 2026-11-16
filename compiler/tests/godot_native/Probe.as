package {
	import godot.*;

	// Run twice by run_godot.py, by the interpreter and as native code: what each method gives must be the
	// same text. The interpreter asks Godot for every operation on a Vector2, so the same text also says
	// that the runtime's own maths for it (runtime/godot_values.h) is Godot's to the last bit.
	public class Probe extends Node2D {
		private var total:Number = 0;
		private var kept:Vector2 = new Vector2(1.5, -2.25);
		private var log:Array = [];
		private var heard:Array = [];
		[Signal] public var struck:Signal;

		// Every operation generated code does inline on a Vector2, on values a float can't hold exactly,
		// on large and small ones, and on the zero vector.
		public function vectors():String {
			var out:Array = [];
			var pairs:Array = [0.1, 0.7, -3.3, 1e-20, 12345.678, -0.000123, 0, 0, 1e19, 1e19, 3, 4, 1 / 3, -2 / 3];
			for (var i:int = 0; i < pairs.length; i += 2) {
				var a:Vector2 = new Vector2(pairs[i], pairs[i + 1]);
				var b:Vector2 = new Vector2(pairs[i + 1] * 1.7, pairs[i] - 0.3);
				var sum:Vector2 = a + b;
				var less:Vector2 = a - b;
				var scaled:Vector2 = a * 0.37;
				var cut:Vector2 = a / 3.3;
				var unit:Vector2 = a.normalized();
				out.push(sum.x, sum.y, less.x, less.y, scaled.x, scaled.y, cut.x, cut.y, unit.x, unit.y, a.length(), unit.length());
			}
			var walk:Vector2 = new Vector2(0, 0);
			var step:Vector2 = new Vector2(0.5, 0.25);
			for (var n:int = 0; n < 1000; n++) {
				walk = walk + step * 0.5;
				walk = walk.normalized() * 3.0;
			}
			out.push(walk.x, walk.y, new Vector2(2, 0).x);
			var part:Vector2 = new Vector2(1, 2);
			part.x = 0.1;
			part.y = part.x * 3 - 7.7;
			part.x += part.y;
			out.push(part.x, part.y);
			return out.join(" ");
		}

		// Properties and methods of the script's own Godot object and of a singleton, called on bare values,
		// and what generated code leaves to the library: a String property, a value kept in a field, a
		// value held untyped.
		public function engine():String {
			var out:Array = [];
			position = new Vector2(3.25, -7.5);
			position = position + new Vector2(0.125, 0.5);
			rotation = 0.75;
			rotation += 0.001;
			out.push(position.x, position.y, rotation, isInsideTree(), Input.isActionPressed("ui_accept"), getChildCount());
			scale = scale * 2.0;
			visible = !visible;
			out.push(scale.x, visible, zIndex);
			name = "probe";
			out.push(name, kept.x, kept.y);
			kept = kept + position;
			var any:* = kept;
			var back:Vector2 = any;
			out.push(back.x, back.y, String(any), kept.length());
			var child:Node2D = new Node2D();
			addChild(child);
			child.position = new Vector2(9, 8);
			out.push(getChildCount(), child.position.x + child.position.y, child.getParent() == this);
			child.queueFree();
			return out.join(" ");
		}

		// The script's own code, with nothing of Godot's: arithmetic, Strings, an Array, a function value,
		// an error caught.
		public function script(rounds:int):String {
			var checksum:int = 0;
			for (var i:int = 0; i < rounds; i++) {
				total += scaled(i, 0.5);
				if ((i & 7) == 0) {
					checksum ^= bump(i);
				}
			}
			var twice:Function = function (x:Number):Number { return x * 2; };
			log.push(twice(total), "round " + rounds);
			try {
				var nothing:Probe = null;
				nothing.total = 1;
			} catch (e:TypeError) {
				log.push(e.errorID);
			}
			return checksum + " " + total + " " + log.join("|");
		}

		// Two listeners that both fail: the first one's error comes out of emit(), where it is caught; Godot
		// goes on to the second all the same, whose error is reported as it ends.
		public function listeners():String {
			struck.add(first);
			struck.add(second);
			try {
				struck.emit(3);
			} catch (e:Error) {
				heard.push("caught " + e.message);
			}
			return heard.join("|");
		}

		private function first(n:int):void {
			heard.push("first " + n);
			throw new Error("from the first");
		}

		private function second(n:int):void {
			heard.push("second " + n);
			throw new Error("from the second");
		}

		private var mute:Boolean = false;

		public function toString():String {
			if (mute) {
				throw new Error("nothing to say");
			}
			return "the probe";
		}

		// Godot puts one of its objects into words by running its script: an error there comes out of the
		// join() that asked, where it is caught.
		public function speaks():String {
			var nodes:Array = [getParent().getChild(getIndex())];
			var said:String = nodes.join("+");
			mute = true;
			try {
				said += nodes.join("+");
			} catch (e:Error) {
				said += " caught " + e.message;
			}
			mute = false;
			return said + " " + nodes.join("+");
		}

		// An error nothing catches: Godot is told, with the trace, and carries on.
		public function fail(depth:int):int {
			if (depth == 0) {
				var none:Array = null;
				return none.length;
			}
			return fail(depth - 1) + 1;
		}

		private function scaled(value:int, by:Number):Number {
			return value * by + 1;
		}

		private function bump(value:int):int {
			return value % 97 + log.length;
		}
	}
}
