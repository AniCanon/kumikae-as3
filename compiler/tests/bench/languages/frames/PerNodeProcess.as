package frames {
	import godot.Node2D;

	public class PerNodeProcess extends Node2D {
		private static const COUNT:int = 20000;

		private var children:Vector.<Node2D> = new Vector.<Node2D>();

		override public function _ready():void {
			for (var i:int = 0; i < COUNT; i++) {
				var child:Node2D = new PerNodeChild();
				addChild(child);
				children.push(child);
			}
		}

		public function checksum():Number {
			var sum:Number = 0;
			for (var i:int = 0; i < children.length; i++) {
				var child:Node2D = children[i];
				sum += child.rotation + child.position.x;
			}
			return sum;
		}
	}
}
