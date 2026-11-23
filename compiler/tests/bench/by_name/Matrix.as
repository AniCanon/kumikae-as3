package {
	import godot.*;

	public class Matrix extends Node2D {
		[Export] public var score:int = 7;
		private var targets:Array = [];
		public function current():int { return score; }
		public function addTarget(value:*):void { targets.push(value); }

		public function probeForeign(target:*, read:Boolean):int {
			try {
				if (read) return int(target.score);
				return int(target.current());
			} catch (e:Error) {
				return -e.errorID;
			}
		}

		public function selfRead(n:int):int { return scriptRead(this, n); }
		public function selfWrite(n:int):void { scriptWrite(this, n); }
		public function selfCall(n:int):int { return scriptCall(this, n); }

		public function scriptRead(target:*, n:int):int {
			var sum = 0;
			for (var i:int = 0; i < n; i++) sum += int(target.score);
			return sum;
		}
		public function scriptWrite(target:*, n:int):void {
			for (var i:int = 0; i < n; i++) target.score = i;
		}
		public function scriptCall(target:*, n:int):int {
			var sum = 0;
			for (var i:int = 0; i < n; i++) sum += int(target.current());
			return sum;
		}
		public function valueRead(target:*, n:int):Number {
			var sum = 0;
			for (var i:int = 0; i < n; i++) sum += Number(target.x);
			return sum;
		}
		public function valueCall(target:*, n:int):Number {
			var sum = 0;
			for (var i:int = 0; i < n; i++) sum += Number(target.length());
			return sum;
		}
		public function nodeRead(target:*, n:int):Number {
			var sum = 0;
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
		public function aliasRead(source:Node2D, n:int):Number {
			var target = source;
			var sum = 0;
			for (var i:int = 0; i < n; i++) {
				var point:Vector2 = target.position;
				sum += point.x;
			}
			return sum;
		}
		public function aliasWrite(source:Node2D, n:int):void {
			var target = source;
			var point:Vector2 = new Vector2(8, 9);
			for (var i:int = 0; i < n; i++) {
				target.position = point;
			}
		}
		public function aliasCall(source:Node2D, n:int):void {
			var target = source;
			for (var i:int = 0; i < n; i++) {
				target.rotate(0.0001);
			}
		}
		public function aliasTypedRead(source:Node2D, n:int):Number {
			var target:Node2D = source;
			var sum = 0;
			for (var i:int = 0; i < n; i++) {
				var point:Vector2 = target.position;
				sum += point.x;
			}
			return sum;
		}
		public function aliasTypedWrite(source:Node2D, n:int):void {
			var target:Node2D = source;
			var point:Vector2 = new Vector2(8, 9);
			for (var i:int = 0; i < n; i++) {
				target.position = point;
			}
		}
		public function aliasTypedCall(source:Node2D, n:int):void {
			var target:Node2D = source;
			for (var i:int = 0; i < n; i++) {
				target.rotate(0.0001);
			}
		}
		public function provenRead(source:Node2D, n:int):Number {
			var target:* = new Node2D();
			target.position = new Vector2(3, 4);
			var sum = 0;
			for (var i:int = 0; i < n; i++) {
				var point:Vector2 = target.position;
				sum += point.x;
			}
			target.free();
			return sum;
		}
		public function provenWrite(source:Node2D, n:int):Number {
			var target:* = new Node2D();
			const point:Vector2 = new Vector2(8, 9);
			for (var i:int = 0; i < n; i++) target.position = point;
			var finalPoint:Vector2 = target.position;
			target.free();
			return finalPoint.x + finalPoint.y;
		}
		public function detachedCall(source:Node2D, n:int):Number {
			var target:* = new Node2D();
			for (var i:int = 0; i < n; i++) target.rotate(0.0001);
			var angle:Number = target.rotation;
			target.free();
			return angle;
		}
		public function typedRead(source:Node2D, n:int):Number {
			var target:Node2D = new Node2D();
			target.position = new Vector2(3, 4);
			var sum = 0;
			for (var i:int = 0; i < n; i++) {
				var point:Vector2 = target.position;
				sum += point.x;
			}
			target.free();
			return sum;
		}
		public function typedWrite(source:Node2D, n:int):Number {
			var target:Node2D = new Node2D();
			const point:Vector2 = new Vector2(8, 9);
			for (var i:int = 0; i < n; i++) target.position = point;
			var finalPoint:Vector2 = target.position;
			target.free();
			return finalPoint.x + finalPoint.y;
		}
		public function provenCall(source:Node2D, n:int):Number {
			var sum = 0;
			for (var i:int = 0; i < n; i++) {
				var target:* = new Node2D();
				target.rotate(0.0001);
				sum += finishAngle(target);
			}
			return sum;
		}
		public function typedCall(source:Node2D, n:int):Number {
			var sum = 0;
			for (var i:int = 0; i < n; i++) {
				var target:Node2D = new Node2D();
				target.rotate(0.0001);
				sum += finishAngle(target);
			}
			return sum;
		}
		private function finishAngle(target:Node2D):Number {
			var angle:Number = target.rotation;
			target.free();
			return angle;
		}
		private function dispose(timer:Timer):void { timer.free(); }

		private var numberField:Number = 3;
		private function lengthOf(value:String):int { return value.length; }
		private function flag():Boolean { return true; }

		public function configureTypedRead(source:Node2D, n:int):Number {
			var sum = 0;
			for (var i:int = 0; i < n; i++) {
				var timer:Timer = new Timer();
				timer.waitTime = 2.5;
				timer.oneShot = true;
				timer.autostart = true;
				sum += timer.waitTime;
				source.addChild(timer);
				dispose(timer);
			}
			return sum;
		}
		public function configureRead(source:Node2D, n:int):Number {
			var sum = 0;
			for (var i:int = 0; i < n; i++) {
				var timer:* = new Timer();
				timer.waitTime = 2.5;
				timer.oneShot = true;
				timer.autostart = true;
				sum += timer.waitTime;
				source.addChild(timer);
				dispose(timer);
			}
			return sum;
		}
		public function timerRead(source:Node2D, n:int):Number {
			var timer:* = new Timer();
			var sum = 0;
			for (var i:int = 0; i < n; i++) sum += timer.waitTime;
			timer.free();
			return sum;
		}
		public function timerTypedRead(source:Node2D, n:int):Number {
			var timer:Timer = new Timer();
			var sum = 0;
			for (var i:int = 0; i < n; i++) sum += timer.waitTime;
			timer.free();
			return sum;
		}
		public function numberRead(source:Node2D, n:int):Number {
			var alias:* = this.numberField;
			var sum = 0;
			for (var i:int = 0; i < n; i++) {
				sum += alias;
			}
			return sum;
		}

		public function numberTypedRead(source:Node2D, n:int):Number {
			var alias:Number = this.numberField;
			var sum = 0;
			for (var i:int = 0; i < n; i++) {
				sum += alias;
			}
			return sum;
		}

		public function booleanRead(source:Node2D, n:int):Number {
			var alias:* = flag();
			var sum = 0;
			for (var i:int = 0; i < n; i++) {
				if (alias) sum += 3;
			}
			return sum;
		}

		public function booleanTypedRead(source:Node2D, n:int):Number {
			var alias:Boolean = flag();
			var sum = 0;
			for (var i:int = 0; i < n; i++) {
				if (alias) sum += 3;
			}
			return sum;
		}

		public function stringRead(source:Node2D, n:int):Number {
			var alias:* = "abc";
			var sum = 0;
			for (var i:int = 0; i < n; i++) {
				sum += lengthOf(alias);
			}
			return sum;
		}

		public function stringTypedRead(source:Node2D, n:int):Number {
			var alias:String = "abc";
			var sum = 0;
			for (var i:int = 0; i < n; i++) {
				sum += lengthOf(alias);
			}
			return sum;
		}

		public function as3ClassRead(source:Node2D, n:int):Number {
			var alias:* = new MatrixScore();
			var sum = 0;
			for (var i:int = 0; i < n; i++) {
				sum += alias.value;
			}
			return sum;
		}

		public function as3ClassTypedRead(source:Node2D, n:int):Number {
			var alias:MatrixScore = new MatrixScore();
			var sum = 0;
			for (var i:int = 0; i < n; i++) {
				sum += alias.value;
			}
			return sum;
		}

		public function vectorRead(source:Vector2, n:int):Number {
			var alias:* = source;
			var sum = 0;
			for (var i:int = 0; i < n; i++) {
				sum += alias.x;
			}
			return sum;
		}

		public function vectorTypedRead(source:Vector2, n:int):Number {
			var alias:Vector2 = source;
			var sum = 0;
			for (var i:int = 0; i < n; i++) {
				sum += alias.x;
			}
			return sum;
		}

		public function colorRead(source:Color, n:int):Number {
			var alias:* = source;
			var sum = 0;
			for (var i:int = 0; i < n; i++) {
				sum += alias.g;
			}
			return sum;
		}

		public function colorTypedRead(source:Color, n:int):Number {
			var alias:Color = source;
			var sum = 0;
			for (var i:int = 0; i < n; i++) {
				sum += alias.g;
			}
			return sum;
		}

		public function mixedRead(source:Node2D, n:int):Number {
			var target = source;
			var sum = 0;
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

final class MatrixScore {
	public var value:int = 3;
}
