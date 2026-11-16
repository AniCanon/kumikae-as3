package {
	import godot.*;

	// A Godot object with a script of AS3's whose object holds a plain one, which holds the Godot object
	// back: a loop through Godot with no listener in it.
	public class Looped extends Resource {
		public var back:Holder;
		public function hold():void { var h:Holder = new Holder(); h.res = this; back = h; }
	}
}
