package {
	import godot.*;

	// Listening to Godot's signals the AS3 way. signals.gd drives each method and counts what happened.
	public class Signals extends Node {
		public var count:int = 0;
		public var last:String = "";
		private var helper:Listener;
		public static var held:Resource;

		private function onRenamed():void { count++; }
		public function onChild(child:Node):void { last = child.name; count += 10; }
		private function onceOnly():void { count += 100; }
		private function removesItself():void { count++; renamed.remove(removesItself); }
		private function fails():void { var n:Node; n.getChildCount(); }
		public function onHit(n:int):void { count += n; }
		private function isBig(n:int):Boolean { return n > 2; }

		public function reset():void { count = 0; }
		// A signal of the script's own, which Godot knows by its name.
		[Signal] public var scored:Signal;
		public function listenOwn():void { scored.add(onHit); }
		public function score(n:int):void { scored.emit(n); }
		public function ownSignalName():String { return scored.getName(); }
		// A loop of a listener and what it listens to; a listener nothing else holds; a signal in a field.
		private var mine:Signal;
		public function loop(r:Resource):void { var h:Holder = new Holder(); h.res = r; r.changed.add(h.heard); }
		// The same, with the object held two steps away, and the two holders holding each other.
		public function loopFar(r:Resource):void {
			var h:Holder = new Holder();
			var far:Holder = new Holder();
			far.res = r;
			far.next = h;
			h.next = far;
			r.changed.add(h.heard);
		}
		// Loops the cycle collector frees: script objects that hold a Godot object and each other, with no
		// listener; a loop through a Godot object with a script of AS3's; a listener whose object holds what
		// it listens to through an untyped field; one listener in the signals of two objects, both held by
		// the loop (GDScript connects the copy).
		public function plainLoop(r:Resource):void {
			var h:Holder = new Holder();
			var g:Holder = new Holder();
			h.res = r;
			h.next = g;
			g.next = h;
		}
		public function untypedLoop(r:Resource):void { var h:Holder = new Holder(); h.any = r; r.changed.add(h.heard); }
		public function loopTwice(r:Resource, other:Resource):void {
			var h:Holder = new Holder();
			h.res = r;
			h.other = other;
			r.changed.add(h.heard);
		}
		// A listener that was removed again, while a list of the signal's listeners is still kept: the
		// callable in that list can still be called, so its object and what it holds must stay whole.
		private var keptList:GodotArray;
		public function removedButKept(r:Resource):void {
			var h:Holder = new Holder();
			h.res = r;
			r.changed.add(h.heard);
			keptList = r.getSignalConnectionList("changed");
			r.changed.remove(h.heard);
		}
		public function unheld(other:Node):void { other.renamed.add(new Listener().heard); }
		public function total():int { return Listener.total; }
		public function byOwnField(name:String):void { mine = renamed; this[name].add(onRenamed); }
		// What the review of this slice left open.
		public function mixed():void { renamed.add(onRenamed); renamed.addOnce(onRenamed); }
		public function mixedBack():void { renamed.addOnce(onceOnly); renamed.add(onceOnly); }
		public function empty():void { new Signal().add(onRenamed); }
		public function onPing(n:Node):void { count += n == null ? 1000 : 1; }
		public function ping(other:Node):void { new Signal(other, "ping").add(onPing); new Signal(other, "ping").emit(null); }
		public function note():Boolean { count += 50; return false; }
		public function wrongName(other:Node):void { other["nothingAtAll"].add(onRenamed, note()); }
		// A plain listener on a signal of something only a static field holds: both go when the program does.
		public function keepPlain(r:Resource):void { held = r; helper = new Listener(); r.changed.add(helper.heard); }
		public function listen():void { renamed.add(onRenamed); }
		public function listenTwice():void { renamed.add(onRenamed); renamed.add(this.onRenamed); }
		public function listenLater(other:Node):void { other.renamed.add(onRenamed, true); }
		public function listenOnce():void { renamed.addOnce(onceOnly); }
		public function stop():void { renamed.remove(onRenamed); }
		public function stopAll():void { renamed.removeAll(); }
		public function isListening():Boolean { return renamed.has(onRenamed); }
		public function listeners():int { return renamed.numListeners; }
		public function fire():void { renamed.emit(); }
		public function listenChild():void { childEnteredTree.add(onChild); }
		public function listenSelfRemoving():void { renamed.add(removesItself); }
		public function listenFailing():void { renamed.add(fails); renamed.add(onRenamed); }
		public function listenTo(other:Node):void { other.renamed.add(onRenamed); }
		public function listenByName(other:Node, signal:String):void { other[signal].add(onRenamed); }
		// A signal's property and its emit(), by its name.
		public function listenersByName(other:Node, signal:String):int { return other[signal].numListeners; }
		public function emitByName(other:Node, signal:String):void { other[signal].emit(); }
		// An error in a listener that this script's own emit() ran comes back out of emit(), as in Flash.
		public function catchesListener():String {
			renamed.add(fails);
			var said:String = "not caught";
			try {
				renamed.emit();
			} catch (e:TypeError) {
				said = "caught " + e.errorID;
			}
			renamed.remove(fails);
			return said;
		}
		public function plain(other:Node):void { helper = new Listener(); other.renamed.add(helper.heard); }
		public function plainCount():int { return helper.count; }
		public function filtered(a:GodotArray):GodotArray { return a.filter(isBig); }
		public function userSignal(other:Node):void { new Signal(other, "hit").add(onHit); }
		public function fireUser(other:Node, n:int):void { new Signal(other, "hit").emit(n); }
	}
}
