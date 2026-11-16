package frames {
	import godot.Node2D;
	import godot.Vector2;

	public class MoveNodes extends Node2D {
		private static const COUNT:int = 20000;

		private var children:Vector.<Node2D> = new Vector.<Node2D>();
		private var velocity:Vector2 = new Vector2(60, 30);

		override public function _ready():void {
			for (var i:int = 0; i < COUNT; i++) {
				var child:Node2D = new Node2D();
				child.position = new Vector2(i % 1000, int(i / 1000));
				addChild(child);
				children.push(child);
			}
		}

		override public function _process(delta:Number):void {
			var n:int = children.length;
			for (var i:int = 0; i < n; i++) {
				var child:Node2D = children[i];
				var p:Vector2 = child.position;
				p = p + velocity * delta;
				if (p.x > 1000) p.x -= 1000;
				child.position = p;
			}
		}

		public function checksum():Number {
			var sum:Number = 0;
			for (var i:int = 0; i < children.length; i++) {
				sum += children[i].position.x;
			}
			return sum;
		}
	}
}
