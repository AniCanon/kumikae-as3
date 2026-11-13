package {
	// A class with no Godot class behind it can listen to a signal too.
	public class Listener {
		public static var total:int = 0;
		public var count:int = 0;
		public function heard():void { count++; total++; }
	}
}
