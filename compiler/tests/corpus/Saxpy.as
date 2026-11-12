package {
	import godot.Node2D;

	// Holdout: a unit-stride numeric loop.
	public class Saxpy extends Node2D {
		private static const N:int = 100000;
		private static const PASSES:int = 10;
		private static const A:Number = 0.000001;

		private var x:Vector.<Number> = new Vector.<Number>(N, true);
		private var y:Vector.<Number> = new Vector.<Number>(N, true);
		private var last:Number = 0;

		override public function _ready():void {
			for (var i:int = 0; i < N; i++) {
				x[i] = i * 0.5;
				y[i] = i;
			}
		}

		override public function _process(delta:Number):void {
			var i:int;
			for (var p:int = 0; p < PASSES; p++) {
				for (i = 0; i < N; i++) {
					y[i] = A * x[i] + y[i];
				}
			}
			var sum:Number = 0;
			for (i = 0; i < N; i++) {
				sum += y[i];
			}
			last = sum;
		}

		public function checksum():Number {
			return last;
		}
	}
}
