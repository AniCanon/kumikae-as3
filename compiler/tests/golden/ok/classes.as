package game.actors {
	import godot.CharacterBody2D;
	import godot.Vector2;

	public final class Player extends CharacterBody2D implements IDamageable {
		public static const MAX_HP:int = 100;
		private var _hp:int = MAX_HP;
		protected var velocity:Vector2 = new Vector2(0, 0);

		public function Player(name:String = "hero") {
			super();
		}

		override public function _process(delta:Number):void {
			position.x += velocity.x * delta;
		}

		public function get hp():int { return _hp; }
		public function set hp(value:int):void { _hp = value < 0 ? 0 : value; }
		internal static function create(...names):Player { return new Player(names[0]); }
		public native function fast(x:int):int;
	}
}

class Helper { }
