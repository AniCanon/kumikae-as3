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
		public function timerInferred(n:int, after:Function):Number {
			var timer:* = new Timer();
			var sum:Number = 0;
			for (var i:int = 0; i < n; i++) sum += timer.waitTime;
			timer.free();
			after();
			return sum;
		}
		public function timerDeclared(n:int, after:Function):Number {
			var timer:Timer = new Timer();
			var sum:Number = 0;
			for (var i:int = 0; i < n; i++) sum += timer.waitTime;
			timer.free();
			after();
			return sum;
		}
		public function beforeEscape(publish:Function):Number {
			var timer:* = new Timer();
			var first:Number = timer.waitTime;
			publish(timer);
			var second:Number = timer.waitTime;
			return first + second;
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
