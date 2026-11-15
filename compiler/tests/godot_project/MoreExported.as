package {
	import godot.*;

	// A subclass of a script with exported fields: it has those, and one of its own.
	public class MoreExported extends Exported {
		[Export] public var extra:int = 2;
		public function sum():Number { return speed + extra; }
	}
}
