package {
	import godot.*;
	public class CmpAs extends Node2D {
		private var total:Number = 0;
		private var steps:int = 0;
		public function spin(rounds:int):Number {
			var step:Vector2 = new Vector2(0.5, 0.25);
			var pressed:int = 0;
			for (var i:int = 0; i < rounds; i++) {
				position = position + step;
				rotation += 0.001;
				if (isInsideTree()) {
					pressed++;
				}
				if (Input.isActionPressed("ui_accept")) {
					pressed--;
				}
			}
			return position.x + pressed;
		}
		public function work(n:int):int {
			var checksum:int = 0;
			for (var i:int = 0; i < n; i++) {
				total += scaled(i, 0.5);
				if ((i & 1023) == 0) {
					checksum ^= bump(i);
				}
			}
			return checksum;
		}
		private function scaled(value:int, by:Number):Number {
			return value * by + 1;
		}
		private function bump(value:int):int {
			steps++;
			return value % 97 + steps;
		}
		public function vec(n:int):Number {
			var v:Vector2 = new Vector2(0, 0);
			var d:Vector2 = new Vector2(0.5, 0.25);
			for (var i:int = 0; i < n; i++) {
				v = v + d * 0.5;
				v = v.normalized() * 3.0;
			}
			return v.x;
		}
	}
}
