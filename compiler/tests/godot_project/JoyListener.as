package {
	import godot.*;

	// Another class that listens to a singleton's signal, for statics.gd: removeAll() on that signal from
	// one script leaves this one's listener.
	public class JoyListener extends Node {
		public var joys:int = 0;
		public function listen():void { Input.joyConnectionChanged.add(onJoy); }
		public function clear():void { Input.joyConnectionChanged.removeAll(); }
		public function listeners():int { return Input.joyConnectionChanged.numListeners; }
		private function onJoy(device:int, connected:Boolean):void { joys += device; }
	}
}
