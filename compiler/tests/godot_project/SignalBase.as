package {
	import godot.*;

	// Signals kept in fields and reached by their names (signals.gd): a base's private fields, and a
	// subclass's fields of the same names.
	public class SignalBase extends Node {
		private var hit:Signal;
		private var secret:Signal;
		private var shout:Signal;
		protected var guarded:Signal;
		public var heard:int = 0;

		public function heardOne():void { heard++; }
		public function prepare():void { hit = renamed; secret = treeExiting; shout = renamed; guarded = treeExited; }
		// From the base's code: its own private field, never a subclass's private one; a subclass's public
		// field before its own private one.
		public function listenFromBase(name:String):void { this[name].add(heardOne); }
	}
}
