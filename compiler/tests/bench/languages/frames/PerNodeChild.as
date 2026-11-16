package frames {
	import godot.Node2D;

	public class PerNodeChild extends Node2D {
		override public function _process(delta:Number):void {
			rotation += delta;
			position.x += 10 * delta;
		}
	}
}
