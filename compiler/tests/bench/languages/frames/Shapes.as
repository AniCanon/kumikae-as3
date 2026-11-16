package frames {
	import godot.Node2D;

	// Holdout: objects and overridable method calls.
	public class Shapes extends Node2D {
		private static const COUNT:int = 9000;
		private static const PASSES:int = 10;

		private var shapes:Vector.<Shape> = new Vector.<Shape>();
		private var total:Number = 0;

		override public function _ready():void {
			for (var i:int = 0; i < COUNT; i++) {
				var size:Number = 1.0 + i * 0.001;
				switch (i % 3) {
					case 0: shapes.push(new Circle(size)); break;
					case 1: shapes.push(new Rect(size, 2.0)); break;
					default: shapes.push(new Tri(3.0, size));
				}
			}
		}

		override public function _process(delta:Number):void {
			var n:int = shapes.length;
			var sum:Number = total;
			for (var p:int = 0; p < PASSES; p++) {
				for (var i:int = 0; i < n; i++) {
					var s:Shape = shapes[i];
					s.grow(0.001);
					sum += s.area();
				}
			}
			total = sum;
		}

		public function checksum():Number {
			return total;
		}
	}
}

class Shape {
	public function area():Number { return 0; }
	public function grow(d:Number):void { }
}

final class Circle extends Shape {
	private var r:Number;
	public function Circle(r:Number) { this.r = r; }
	override public function area():Number { return Math.PI * r * r; }
	override public function grow(d:Number):void { r += d; }
}

final class Rect extends Shape {
	private var w:Number;
	private var h:Number;
	public function Rect(w:Number, h:Number) { this.w = w; this.h = h; }
	override public function area():Number { return w * h; }
	override public function grow(d:Number):void { w += d; h += d; }
}

final class Tri extends Shape {
	private var b:Number;
	private var h:Number;
	public function Tri(b:Number, h:Number) { this.b = b; this.h = h; }
	override public function area():Number { return 0.5 * b * h; }
	override public function grow(d:Number):void { b += d; }
}
