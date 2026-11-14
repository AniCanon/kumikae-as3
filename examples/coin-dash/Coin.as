package {
	import godot.*;

	// A coin: it pulses, and when the player touches it, it is counted and appears somewhere else.
	public class Coin extends Area2D {
		public static var collected:int = 0;
		private var age:Number = 0;

		override public function _ready():void {
			bodyEntered.add(onTouched);
		}

		override public function _process(delta:Number):void {
			age += delta;
			queueRedraw();
		}

		override public function _draw():void {
			drawCircle(Vector2.ZERO, 14 + sin(age * 6) * 3, new Color(1.0, 0.82, 0.25));
		}

		private function onTouched(body:Node2D):void {
			collected++;
			position = new Vector2(randfRange(60, 900), randfRange(90, 480));
		}
	}
}
