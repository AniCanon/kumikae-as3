package {
	import godot.*;

	// A plain object that listens to a signal and also holds the signal's object: a loop that only the
	// two of them are part of. Both must go when nothing else refers to either.
	public class Holder {
		public var res:Resource;
		public var other:Resource;
		public var any:*;
		public var next:Holder;
		public function heard():void {}
	}
}
