package {
	import godot.Node;
	import godot.Signal;

	public class Signals extends Node {
		[Signal] public var fine:Signal;
		[Signal(args="int")] public var given:Signal;
		[Signal] public static var shared:Signal;
		[Signal] public var count:int;
		[Signal] public var written:Signal = new Signal();
	}
}

import godot.Signal;

class Plain {
	[Signal] public var nowhere:Signal;
}
