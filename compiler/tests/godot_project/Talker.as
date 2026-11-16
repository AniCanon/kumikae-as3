package {
	import godot.*;

	// A Resource with a signal of its own, that a function written in its method listens to (cycles.gd): the
	// function's environment holds the object, the signal holds the function, and nothing else either.
	public class Talker extends Resource {
		[Signal] public var said:Signal;
		public var heard:int;
		public function listen():void { said.add(function ():void { heard++; }); }
		public function say():int { said.emit(); return heard; }
	}
}
