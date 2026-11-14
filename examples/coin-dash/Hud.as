package {
	import godot.*;

	// The score, in the corner.
	public class Hud extends Label {
		override public function _process(delta:Number):void {
			text = "Coins: " + Coin.collected;
		}
	}
}
