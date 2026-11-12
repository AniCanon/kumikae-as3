package {
	import godot.Node2D;
	import godot.Vector2;

	[Signal(name="hit", args="amount:int, pos:Vector2")]
	public class SignalTraffic extends Node2D {
		private static const EMITS:int = 20000;

		private var total:Number = 0;

		override public function _ready():void {
			connect("hit", on_hit);
		}

		override public function _process(delta:Number):void {
			for (var i:int = 0; i < EMITS; i++) {
				emit_signal("hit", i, new Vector2(i, 1));
			}
		}

		public function on_hit(amount:int, pos:Vector2):void {
			total += amount + pos.x;
		}

		public function checksum():Number {
			return total;
		}
	}
}
