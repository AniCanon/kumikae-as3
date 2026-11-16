package {
	import godot.*;

	// Godot calling into a script, every way it does: a method by name with all its arguments, with some
	// left out, with more than it names; a listener of a signal; a call that comes back in through Godot
	// again and again. Run by the interpreter and as native code (probe.gd): each must say the same, and
	// report the same errors with the same traces.
	public class Entry extends Node2D {
		private var heard:Array = [];
		private var raising:int = 0;
		[Signal] public var rang:Signal;

		public function defaults(a:int, b:int = 5, c:String = "x", d:Number = 0.5):String {
			return a + " " + b + " " + c + " " + d;
		}

		public function rest(first:int, ...more):String {
			return first + " and " + more.length + ": " + more.join(",");
		}

		public function gives(what:int):* {
			switch (what) {
				case 0: return "text";
				case 1: return this;
				case 2: return new Vector2(1, 2);
				case 3: return 4000000000;
				case 4: return [1, "two", 3.5];
				default: return null;
			}
		}

		public function takes(at:Vector2, node:Node2D, text:String, list:Array):String {
			return at + " " + (node === this) + " " + text + " " + (list == null ? "no list" : list.join("+"));
		}

		// Godot calls this method, which asks Godot to call it again: each is a call from outside inside the
		// last. They nest only so deep, and the one too many is Error #1023, here where it can be caught.
		public function again(left:int):int {
			if (left == 0) {
				return 0;
			}
			return int(call("again", left - 1)) + 1;
		}

		public function nested(depth:int):String {
			try {
				return "reached " + again(depth);
			} catch (e:Error) {
				return "caught " + e.message;
			}
			return "neither";
		}

		// A listener that fails while Godot runs a method the script called on bare values (hide() tells
		// the listeners of visibilityChanged before it returns), with nothing to catch it: the method ends
		// there, and Godot is told with both functions in the trace.
		private function onShown():void {
			heard.push("shown " + raising);
			if (raising > 0) {
				throw new Error("in the first listener");
			}
		}

		private function onShownToo():void {
			heard.push("too " + raising);
			if (raising > 1) {
				throw new Error("in the second listener");
			}
		}

		public function listen():void {
			visibilityChanged.add(onShown);
			visibilityChanged.add(onShownToo);
			rang.add(onRang);
		}

		public function unheard():String {
			raising = 1;
			hide();
			return "not reached";
		}

		// Both listeners fail: the first one's error comes out of show(), the second's is reported where
		// that listener ends.
		public function both():String {
			raising = 2;
			var said:String = "";
			try {
				show();
				said += "shown ";
			} catch (e:Error) {
				said += "caught " + e.message + " ";
			} finally {
				said += "finally ";
			}
			raising = 0;
			hide();
			show();
			return said + heard.join("|");
		}

		// A listener that emits the signal it listens to, until it has heard enough: listeners inside
		// listeners, each entered by Godot.
		private function onRang(left:int, at:Vector2):void {
			heard.push(left + "@" + at.x);
			if (left > 0) {
				rang.emit(left - 1, at * 2);
			}
			if (left == 2) {
				throw new Error("at two");
			}
		}

		public function rings():String {
			heard = [];
			try {
				rang.emit(4, new Vector2(1, 1));
			} catch (e:Error) {
				heard.push("caught " + e.message);
			}
			return heard.join("|");
		}

		private var ticks:int = 0;

		// Godot's own call, each frame: the third fails, and the fourth still runs.
		override public function _process(delta:Number):void {
			ticks++;
			if (ticks == 3) {
				var none:Entry = null;
				none.ticks = 1;
			}
		}

		public function ticked():int {
			return ticks;
		}
	}
}
