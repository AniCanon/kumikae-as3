package {
	import godot.*;

	public class Proven extends Node {
		public function inferred():Number {
			var target:* = new Node2D();
			target.position = new Vector2(3, 4);
			target.rotate(0.25);
			var result:Vector2 = target.position;
			target.free();
			return result.x;
		}

		public function declared():Number {
			var target:Node2D = new Node2D();
			target.position = new Vector2(3, 4);
			target.rotate(0.25);
			var result:Vector2 = target.position;
			target.free();
			return result.x;
		}
		public function acrossCallback(callback:Function):Number {
			var target = new Node2D();
			callback();
			var result:Vector2 = target.position;
			target.free();
			return result.x;
		}
	}
}
