package {
	import godot.*;
	// A script on a Resource, made with `new` by Kept.as.
	public class Stored extends Resource {
		public var label:String;
		public var heard:int;
		// It listens to its own signal: the listener must not keep it alive.
		public function listen():void { changed.add(onChanged); }
		private function onChanged():void { heard++; }
	}
}
