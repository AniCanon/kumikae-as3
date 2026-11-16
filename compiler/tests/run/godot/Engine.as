package {
	import godot.*;

	// Each kind of request to the engine, for the IR the extension runs (`kas3c ir --godot`).
	public class Engine extends Node2D {
		public function values(n:int):Number {
			var v:Vector2 = new Vector2(0, 0);
			var d:Vector2 = Vector2.UP;
			for (var i:int = 0; i < n; i++) {
				v = v + d * 0.5;
				v = -v.normalized() * 3.0;
			}
			var same:Boolean = v == d;
			return v.x + (same ? 1 : 0);
		}
		public function members():Boolean {
			position = position + Vector2.fromAngle(rotation);
			rotation += lerpf(0, 1, 0.5);
			var other:Node = new Node();
			var sprite:Sprite2D = other as Sprite2D;
			other.free();
			return isInsideTree() && Input.isActionPressed("ui_accept") && other is Node2D && sprite == null;
		}
		public function collections(points:Vector.<Vector2>):int {
			var line:Line2D = new Line2D();
			line.points = points;
			var total:int = 0;
			for each (var p:Vector2 in line.points) {
				total++;
			}
			line.free();
			return total;
		}
		public function defaults(c:Color):Color {
			return c.clamp(null, c);
		}
		public function signals():void {
			ready.add(heard);
		}
		private function heard():void {
		}
	}
}
