package {
	import godot.*;

	// The player: a disc moved with the arrow keys or a gamepad's stick.
	public class Player extends CharacterBody2D {
		override public function _physicsProcess(delta:Number):void {
			// Try another speed while the game runs: save, and the disc moves at it.
			velocity = Input.getVector("ui_left", "ui_right", "ui_up", "ui_down") * 320;
			moveAndSlide();
			position = new Vector2(clampf(position.x, 22, 938), clampf(position.y, 22, 518));
			queueRedraw();
		}

		override public function _draw():void {
			// Or another colour, or size.
			drawCircle(Vector2.ZERO, 22, new Color(0.35, 0.75, 1.0));
			drawCircle(new Vector2(8, -7), 4, Color.WHITE);
		}
	}
}
