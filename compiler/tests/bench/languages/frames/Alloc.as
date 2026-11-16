package frames {
	import godot.Node2D;

	// Holdout: allocation every frame.
	public class Alloc extends Node2D {
		private static const COUNT:int = 20000;

		private var slots:Vector.<Particle> = new Vector.<Particle>(COUNT, true);
		private var frame:int = 0;
		private var last:Number = 0;

		override public function _process(delta:Number):void {
			frame++;
			var i:int;
			for (i = 0; i < COUNT; i++) {
				slots[i] = new Particle(i, frame, i * 0.5, frame * 0.25);
			}
			var sum:Number = 0;
			for (i = 0; i < COUNT; i++) {
				var p:Particle = slots[i];
				sum += p.x + p.vy;
			}
			last = sum;
		}

		public function checksum():Number {
			return last;
		}
	}
}

final class Particle {
	public var x:Number, y:Number, vx:Number, vy:Number;

	public function Particle(x:Number, y:Number, vx:Number, vy:Number) {
		this.x = x;
		this.y = y;
		this.vx = vx;
		this.vy = vy;
	}
}
