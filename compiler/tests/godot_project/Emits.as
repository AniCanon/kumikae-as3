package {
	import godot.*;

	// What a signal carries: from AS3's emit() to whoever listens, and from Godot to a listener written in
	// AS3. emits_cases.gd drives each method; emits.gd holds what it gives to what GDScript gives, and the
	// native probe (tests/godot_native) to what the interpreter gives.
	public class Emits extends Node {
		[Signal] public var sent:Signal;
		[Signal] public var struck:Signal;
		[Signal] public var again:Signal;
		public var heard:Array = [];
		public var victim:Node;
		private var mine:Signal;
		private static var seen:Array = [];

		public function said():String {
			var out:String = heard.concat(seen).join(";");
			heard = [];
			seen = [];
			return out;
		}

		// Each kind of value an emit() hands over, as it is held where the emit is written: numbers and
		// Booleans, Godot's values with parts in a parameter and made on the spot, and what is held by a
		// handle or untyped.
		public function send(n:int, x:Number, flag:Boolean, at:Vector2):void { sent.emit(n, x, flag, at); }
		public function sendWhole(u:uint, wide:Int64, n:int):void { sent.emit(u, wide, n, n + 0.5); }
		public function sendParts(tint:Color, turn:Transform2D, deep:Vector3, cell:Vector2i):void { sent.emit(tint, turn, deep, cell); }
		public function sendMade():void {
			for (var i:int = 0; i < 3; i++) {
				sent.emit(i, i * 0.5, i == 1, new Vector2(i, -i));
			}
		}
		public function sendHeld(text:String, node:Node, any:*, none:Vector2):void { sent.emit(text, node, any, none); }
		public function sendNothing():void { sent.emit(); }
		public function sendMany():void { sent.emit(1, 2, 3, 4, 5, 6); }

		// One of Godot's own signals, a signal of another object, and one place that emits whichever signal
		// it is handed.
		public function sendRenamed():void { renamed.emit(); }
		public function sendThrough(other:Node, n:int):void {
			var signal:Signal = new Signal(other, "hit");
			signal.emit(n, new Vector2(n, n));
		}
		private function fire(signal:Signal, n:int):void { signal.emit(n, new Vector2(n, 1)); }
		public function sendEach(other:Node):void {
			fire(struck, 1);
			fire(new Signal(other, "hit"), 2);
			fire(struck, 3);
			fire(new Signal(), 4);
		}

		// Listeners written in AS3, each with its parameters typed another way.
		public function onStruck(amount:int, at:Vector2):void { heard.push(amount + " " + at); }
		public function onWide(u:uint, x:Number, flag:Boolean, wide:Int64):void { heard.push(u + " " + x + " " + flag + " " + wide); }
		public function onHeld(text:String, node:Node, any:*, at:Vector2):void {
			heard.push(text + " " + (node == null ? "no node" : node.name) + " " + any + " " + at);
		}
		public function onDefault(amount:int, at:Vector2 = null):void { heard.push("default " + amount + " " + at); }
		public function onRest(amount:int, ...more):void { heard.push("rest " + amount + " " + more.length); }
		public static function onStatic(amount:int, at:Vector2):void { seen.push("static " + amount + " " + at.y); }
		// It changes what it was given: the listener after it is given what was emitted.
		public function onChanged(amount:int, at:Vector2):void {
			at.x = 100;
			amount += 7;
			heard.push("changed " + amount + " " + at);
		}
		// It keeps what it was given, in a field, in an Array and untyped: each is its own once the call is
		// over, whatever Godot does with what it handed over.
		private var keptAt:Vector2;
		private var keptAll:Array = [];
		private var keptAny:*;
		public function onKeep(amount:int, at:Vector2):void {
			keptAt = at;
			keptAll.push(at);
			keptAny = at;
		}
		public function kept():String {
			var out:String = keptAt + " " + keptAll.join("/") + " " + keptAny;
			keptAll = [];
			return out;
		}
		public function onTurned(at:Vector2, amount:int):void { heard.push("turned " + at + " " + amount); }
		public function onGiven(amount:int, at:Vector2):Vector2 {
			at = at + new Vector2(1, 1);
			return at;
		}
		public function onFail(amount:int, at:Vector2):void {
			heard.push("failing " + amount);
			throw new Error("in the listener at " + at.x);
		}
		public function onFree(amount:int, at:Vector2):void {
			heard.push("freeing");
			victim.free();
		}
		public function onSwap(amount:int, at:Vector2):void {
			mine = sent;
			heard.push("swapped " + amount);
		}
		public function onAgain(left:int, at:Vector2):void {
			heard.push(left + "@" + at.x);
			if (left > 0) {
				again.emit(left - 1, at * 2);
			}
		}

		public function listen(how:String):void {
			switch (how) {
				case "struck": struck.add(onStruck); break;
				case "wide": sent.add(onWide); break;
				case "held": sent.add(onHeld); break;
				case "default": struck.add(onDefault); break;
				case "rest": struck.add(onRest); break;
				case "static": struck.add(onStatic); break;
				case "changed": struck.add(onChanged); break;
				case "given": struck.add(onGiven); break;
				case "keep": struck.add(onKeep); break;
				case "turned": struck.add(onTurned); break;
				case "fail": struck.add(onFail); break;
				case "free": struck.add(onFree); break;
				case "later": struck.add(onStruck, true); break;
				case "once": struck.addOnce(onStruck); break;
				case "again": again.add(onAgain); break;
				case "mate": sent.add(onMate); break;
				case "closure": struck.add(function (amount:int, at:Vector2):void { heard.push("closure " + amount + " " + at.x); }); break;
			}
		}
		public function listenTo(other:Node):void { new Signal(other, "struck").add(onStruck); }
		public function forget():void {
			struck.removeAll();
			sent.removeAll();
			again.removeAll();
		}

		// The script's own emit to its own listeners, which is one frame of the benchmark.
		public function strike(n:int):void { struck.emit(n, new Vector2(n, 1)); }
		public function strikes(count:int):void {
			for (var i:int = 0; i < count; i++) {
				struck.emit(i, new Vector2(i, 1));
			}
		}
		public function ring(n:int):void { again.emit(n, new Vector2(1, 1)); }
		public function catches(n:int):String {
			try {
				struck.emit(n, new Vector2(n, 1));
			} catch (e:Error) {
				return "caught " + e.message;
			}
			return "nothing thrown";
		}
		// What the wrong arguments from here are to a listener written in AS3: Godot's own refusal.
		public function strikeWrong():void { struck.emit("five", new Vector3(1, 2, 3)); }
		public function strikeShort():void { struck.emit(5); }

		// A signal kept in a field: of an object freed since, and one a listener puts another in while
		// the emit that runs the listener goes on.
		public function keep(other:Node):void { mine = other == null ? struck : new Signal(other, "hit"); }
		public function keepNone():void { mine = null; }
		public function sendKept(n:int):String {
			try {
				mine.emit(n, new Vector2(n, 0));
			} catch (e:Error) {
				return "caught " + e.message;
			}
			return "sent";
		}
		public function swaps():void {
			mine = struck;
			mine.add(onSwap);
		}
		// What is emitted is worked out by a function that puts another signal in the field: the emit is of
		// the signal the field held when it was read, before the function ran.
		private function swapping(n:int):int {
			mine = sent;
			return n;
		}
		public function sendSwapping(n:int):String {
			mine = struck;
			mine.emit(swapping(n), new Vector2(n, 0));
			return mine == sent ? "swapped" : "kept";
		}

		// A Godot object that carries this script, where an Emits is declared: it is the script's own object,
		// as a node whose script is a C# class is that class. From Godot (a call, a field set, what a signal
		// hands a listener) and in AS3 itself (`as`, `is`, an untyped value stored).
		public var mate:Emits;
		public function nameOf(other:Emits):String { return other ? String(other.name) : "none"; }
		public function mateName():String { return mate ? String(mate.name) : "none"; }
		public function onMate(other:Emits):void { heard.push("mate " + (other ? String(other.name) : "none")); }
		public function asMate(other:Node):String {
			var found:Emits = other as Emits;
			return (other is Emits) + " " + (found ? String(found.name) : "none") + " " + (found != null && found === mate);
		}
		public function untypedMate(other:*):Array { return [other is Emits, (other as Emits) != null, other is EmitsChild, (other as EmitsChild) == null]; }
		public function keepUntyped(other:*):String {
			try {
				mate = other;
			} catch (error:TypeError) {
				return error.message;
			}
			return mateName();
		}
		public function parentMate():String {
			var found:Emits = getParent() as Emits;
			return found ? String(found.name) : "none";
		}
	}
}
