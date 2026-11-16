package frames {
	import godot.Node2D;

	public class Empty extends Node2D {
		override public function _process(delta:Number):void {
		}

		public function checksum():Number {
			return 0;
		}
	}
}
