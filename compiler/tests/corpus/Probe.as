package {
	import godot.Node;

	[Signal(name="pinged")]
	public class Probe extends Node {
		private var count:int;

		override public function _ready():void { count = 0; connect("pinged", on_pinged); }
		override public function _process(delta:Number):void { count++; }
		public function get_count():int { return count; }
		public function explode():void {
			throw new Error("boom");
		}
		public function ping():void { emit_signal("pinged"); }
		public function on_pinged():void { count += 100; }
	}
}
