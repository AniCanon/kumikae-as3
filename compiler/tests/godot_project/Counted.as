package {
	import godot.*;
	public class Counted extends RefCounted {
		public var n:int;
		public var heard:int;
		public static var held:Counted;
		public function hold():void { held = this; }
		public function hello():String { return "hello"; }
		public function listenTo(other:Node):void { other.renamed.add(onRenamed); }
		private function onRenamed():void { heard++; }
	}
}
