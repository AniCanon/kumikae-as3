package {
	// Values that do not fit where they are used.
	public class Mismatch extends Entity implements Scored {
		public const LIVES:int = 3;
		private var names:Vector.<String> = new Vector.<String>();
		private var score:int = "none";

		public function Mismatch() {
			super("hero", 2);
			return this;
		}

		public function get points():int {
			return score;
		}

		override public function update(delta:int):void {
			var best:Mismatch = find("hero");
			var label:String = score;
			names.insertAt(0, score);
			move(1);
			move(1, 2, 3);
			LIVES = 2;
			points = 10;
			update = null;
			return score;
		}

		public function total():Number {
		}
	}
}

class Entity {
	public function Entity(name:String) {
	}

	public function update(delta:Number):void {
	}

	public function move(x:Number, y:Number):void {
	}

	public function find(name:String):Entity {
		return null;
	}
}

interface Scored {
	function get points():int;
	function reset():void;
}
