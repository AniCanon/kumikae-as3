package {
	import godot.Node;
	import godot.Signal;

	public class Probe extends Node {
		[Signal] public var pinged:Signal;
		private var count:int;

		override public function _ready():void { count = 0; pinged.add(onPinged); }
		override public function _process(delta:Number):void { count++; }
		public function getCount():int { return count; }
		public function explode():void {
			throw new Error("boom");
		}
		public function ping():void { pinged.emit(); }
		public function onPinged():void { count += 100; }
	}
}
