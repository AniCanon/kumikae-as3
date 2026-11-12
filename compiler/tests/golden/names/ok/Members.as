package game {
	import godot.Node2D;
	import godot.Vector2;

	public class Members extends Node2D implements Scorer {
		public static const LIMIT:int = 10;
		private static var created:int = 0;
		private var points:int = LIMIT;

		public function Members() {
			super();
			created++;
		}

		override public function _process(delta:Number):void {
			var where:Vector2 = position;
			rotation += delta;
			add_child(new Enemy(this));
			score = points + Enemy.count;
		}

		public function get score():int {
			return points;
		}

		public function set score(value:int):void {
			points = Math.min(value, LIMIT);
		}

		public static function reset():void {
			created = 0;
		}
	}
}

import godot.Node2D;
import game.Members;

interface Scorer {
	function get score():int;
}

class Enemy extends Node2D {
	public static var count:int;
	protected var target:Members;

	public function Enemy(target:Members) {
		this.target = target;
		count++;
	}
}
