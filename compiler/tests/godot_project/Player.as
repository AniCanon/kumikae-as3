package {
	import godot.*;

	public class Player extends CharacterBody2D {
		private var speed:Number = 200;
		public var steps:int = 0;

		override public function _physicsProcess(delta:Number):void {
			velocity = new Vector2(speed, 0);
			moveAndSlide();
			steps++;
		}

		public function fail():void {
			var nobody:Player;
			nobody.steps = 1;
		}

		public function describe(prefix:String):String {
			return prefix + " after " + steps + " steps";
		}
	}
}
