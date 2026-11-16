package frames {
	import godot.Node2D;
	import godot.Signal;
	import godot.Vector2;

	public class SignalTraffic extends Node2D {
		private static const EMITS:int = 20000;

		[Signal] public var hit:Signal;
		private var total:Number = 0;

		override public function _ready():void {
			hit.add(onHit);
		}

		override public function _process(delta:Number):void {
			for (var i:int = 0; i < EMITS; i++) {
				hit.emit(i, new Vector2(i, 1));
			}
		}

		public function onHit(amount:int, pos:Vector2):void {
			total += amount + pos.x;
		}

		public function checksum():Number {
			return total;
		}
	}
}
