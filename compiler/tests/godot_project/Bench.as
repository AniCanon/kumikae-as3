package {
	import godot.*;

	// A benchmark, not a test: calls from AS3 into the engine. See tests/bench/run_bench.py.
	public class Bench extends Node2D {
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
	}
}
