package {
	import godot.Line2D;
	import godot.Node2D;
	import godot.Vector2;

	public class ArrayConversion extends Node2D {
		private static const ROUNDS:int = 20;
		private static const SIZE:int = 1000;

		private var line:Line2D;

		override public function _ready():void {
			line = new Line2D();
			add_child(line);
		}

		override public function _process(delta:Number):void {
			for (var k:int = 0; k < ROUNDS; k++) {
				var points:Vector.<Vector2> = new Vector.<Vector2>();
				for (var i:int = 0; i < SIZE; i++) {
					points.push(new Vector2(i, k));
				}
				line.points = points;
				var data:Array = [];
				for (i = 0; i < SIZE; i++) {
					data.push(i + k);
				}
				set_meta("data", data);
			}
		}

		public function checksum():Number {
			var data:Array = get_meta("data") as Array;
			return line.get_point_count() + data.length;
		}
	}
}
