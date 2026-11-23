package {
	import godot.*;

	public class Matrix extends Node2D {
		[Export] public var score:int = 7;
		private var targets:Array = [];
		public function current():int { return score; }
		public function addTarget(value:*):void { targets.push(value); }

		public function selfRead(n:int):int { return scriptRead(this, n); }
		public function selfWrite(n:int):void { scriptWrite(this, n); }
		public function selfCall(n:int):int { return scriptCall(this, n); }

		public function scriptRead(target:*, n:int):int {
			var sum:int = 0;
			for (var i:int = 0; i < n; i++) sum += int(target.score);
			return sum;
		}
		public function scriptWrite(target:*, n:int):void {
			for (var i:int = 0; i < n; i++) target.score = i;
		}
		public function scriptCall(target:*, n:int):int {
			var sum:int = 0;
			for (var i:int = 0; i < n; i++) sum += int(target.current());
			return sum;
		}
		public function valueRead(target:*, n:int):Number {
			var sum:Number = 0;
			for (var i:int = 0; i < n; i++) sum += Number(target.x);
			return sum;
		}
		public function valueCall(target:*, n:int):Number {
			var sum:Number = 0;
			for (var i:int = 0; i < n; i++) sum += Number(target.length());
			return sum;
		}
		public function nodeRead(target:*, n:int):Number {
			var sum:Number = 0;
			for (var i:int = 0; i < n; i++) {
				var point:Vector2 = target.position;
				sum += point.x;
			}
			return sum;
		}
		public function nodeWrite(target:*, n:int):void {
			var point:Vector2 = new Vector2(8, 9);
			for (var i:int = 0; i < n; i++) {
				target.position = point;
			}
		}
		public function nodeCall(target:*, n:int):void {
			for (var i:int = 0; i < n; i++) {
				target.rotate(0.0001);
			}
		}
		public function provenRead(source:Node2D, n:int):Number {
			var target = source;
			var sum:Number = 0;
			for (var i:int = 0; i < n; i++) {
				var point:Vector2 = target.position;
				sum += point.x;
			}
			return sum;
		}
		public function provenWrite(source:Node2D, n:int):void {
			var target = source;
			var point:Vector2 = new Vector2(8, 9);
			for (var i:int = 0; i < n; i++) {
				target.position = point;
			}
		}
		public function provenCall(source:Node2D, n:int):void {
			var target = source;
			for (var i:int = 0; i < n; i++) {
				target.rotate(0.0001);
			}
		}
		public function typedRead(source:Node2D, n:int):Number {
			var target:Node2D = source;
			var sum:Number = 0;
			for (var i:int = 0; i < n; i++) {
				var point:Vector2 = target.position;
				sum += point.x;
			}
			return sum;
		}
		public function typedWrite(source:Node2D, n:int):void {
			var target:Node2D = source;
			var point:Vector2 = new Vector2(8, 9);
			for (var i:int = 0; i < n; i++) {
				target.position = point;
			}
		}
		public function typedCall(source:Node2D, n:int):void {
			var target:Node2D = source;
			for (var i:int = 0; i < n; i++) {
				target.rotate(0.0001);
			}
		}
		public function mixedRead(source:Node2D, n:int):Number {
			var target = source;
			var sum:Number = 0;
			for (var i:int = 0; i < n; i++) {
				target = targets[i & 7];
				var point:Vector2 = target.position;
				sum += point.x;
			}
			return sum;
		}
		public function mixedWrite(source:Node2D, n:int):void {
			var target = source;
			var point:Vector2 = new Vector2(8, 9);
			for (var i:int = 0; i < n; i++) {
				target = targets[i & 7];
				target.position = point;
			}
		}
		public function mixedCall(source:Node2D, n:int):void {
			var target = source;
			for (var i:int = 0; i < n; i++) {
				target = targets[i & 7];
				target.rotate(0.0001);
			}
		}
	}
}
