package {
	import godot.*;

	// See SignalBase: a private field named as one of the base's, and a public signal named as another.
	public class SignalDerived extends SignalBase {
		private var hit:Signal;
		[Signal] public var shout:Signal;

		public function prepareDerived():void { hit = childEnteredTree; }
		// From the subclass's code: its own private field; the base's private one is not reached.
		public function listenFromDerived(name:String):void { this[name].add(heardOne); }
	}
}
