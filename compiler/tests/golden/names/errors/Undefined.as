package {
	import nowhere.Thing;
	import godot.*;

	public class Undefined extends Node {
		private var held:Thing;
		private static var shared:int;

		public function _ready():void {
			missing = shared + 1;
			var copy:Vector.<Gone> = new <Gone>[];
			outer: for (var i:int = 0; i < 3; i++) {
				continue inner;
			}
			break;
		}

		public static function build():Undefined {
			_process(0);
			return this;
		}

		override public function nothing():void {
			var x:int;
			var x:String;
			f() = x;
		}
	}
}
