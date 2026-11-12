package {
	import godot.Node;

	[Signal(name="boom")]
	public class V2Probe extends Node {
		private var state:int = 0;

		override public function _ready():void { connect("boom", on_boom); }
		public function fire():void { emit_signal("boom"); state++; }
		public function on_boom():void {
			state++;
			throw new Error("in handler");
		}
		public function chain():void { middle(); }
		private function middle():void { leaf(null); }
		private function leaf(o:Object):void { o.toString(); }
		public function get_state():int { return state; }
		public function get_closure():Function { return bump; }
		public function bump():void { state += 10; }
	}
}
