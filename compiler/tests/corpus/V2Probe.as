package {
	import godot.Node;
	import godot.Signal;

	public class V2Probe extends Node {
		[Signal] public var boom:Signal;
		private var state:int = 0;

		override public function _ready():void { boom.connect(onBoom); }
		public function fire():void { boom.emit(); state++; }
		public function onBoom():void {
			state++;
			throw new Error("in handler");
		}
		public function chain():void { middle(); }
		private function middle():void { leaf(null); }
		private function leaf(o:Object):void { o.toString(); }
		public function getState():int { return state; }
		public function getClosure():Function { return bump; }
		public function bump():void { state += 10; }
	}
}
