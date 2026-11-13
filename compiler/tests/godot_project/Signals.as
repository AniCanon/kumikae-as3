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
		public function plain(other:Node):void { helper = new Listener(); other.renamed.add(helper.heard); }
		public function plainCount():int { return helper.count; }
		public function filtered(a:GodotArray):GodotArray { return a.filter(isBig); }
		public function userSignal(other:Node):void { new Signal(other, "hit").add(onHit); }
		public function fireUser(other:Node, n:int):void { new Signal(other, "hit").emit(n); }
	}
}
