package {
	import godot.*;

	public class Untyped extends Node2D {
		[Export] public var score:int = 0;

		public function current():int { return score; }

		public function scriptLoop(rounds:int):int {
			var target:* = this;
			var sum:int = 0;
			for (var i:int = 0; i < rounds; i++) {
				target.score = i & 1;
				sum += int(target.score) + int(target.current());
			}
			return sum;
		}

		public function nodeLoop(target:*, rounds:int):int {
			var sum:int = 0;
			for (var i:int = 0; i < rounds; i++) {
				target.visible = (i & 1) != 0;
				sum += int(target.visible) + int(target.isVisibleInTree());
			}
			return sum;
		}

		public function valueLoop(rounds:int):int {
			var target:* = new Vector2(3, 4);
			var sum:int = 0;
			for (var i:int = 0; i < rounds; i++) {
				sum += int(target.x) + int(target.length());
			}
			return sum;
		}

		public function refusedLoop(rounds:int):int {
			var target:* = new Vector2(3, 4);
			var refused:int = 0;
			for (var i:int = 0; i < rounds; i++) {
				try { target.x = i; } catch (e:Error) { refused++; }
			}
			return refused;
		}
	}
}
