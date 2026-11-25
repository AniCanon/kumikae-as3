package {
	import godot.*;

	public class DirectCalls extends Node2D {
		private var mesh:MultiMesh;

		override public function _process(delta:Number):void {
			var batch:MultiMesh = mesh;
			for (var i:int = 0; i < 16; ++i) {
				var point:Vector2 = new Vector2(i, -i);
				var transform:Transform2D = new Transform2D(delta, new Vector2(2, 3), 0, point);
				var color:Color = new Color(i / 16, 0.5, 0, 1);
				batch.setInstanceTransform2D(0, transform);
				batch.setInstanceCustomData(0, color);
			}
		}

		private function classOf(node:Node):String {
			try {
				return node.getClass();
			} catch (e:Error) {
				return e.name + " " + e.errorID + " " + e.message;
			}
		}

		override public function _ready():void {
			var child:Node2D = new Node2D();
			addChild(child);
			trace("DIRECT live " + classOf(child.getParent()));
			trace("DIRECT path " + classOf(getNodeOrNull(".")));
			trace("DIRECT null " + classOf(null));
			child.free();
			trace("DIRECT freed " + classOf(child));
			var batch:MultiMesh = new MultiMesh();
			batch.useCustomData = true;
			batch.instanceCount = 1;
			mesh = batch;
			var skewed:Transform2D = new Transform2D(0.25, new Vector2(-3, 0.5), -0.125, new Vector2(7, -9));
			var escaped:* = skewed;
			trace("TRANSFORM " + escaped);
			var transform:Transform2D = new Transform2D();
			batch.setInstanceTransform2D(0, transform);
			batch.setInstanceCustomData(0, new Color(1, 0, 0, 1));
			var material:ShaderMaterial = new ShaderMaterial();
			var copy:Resource = material.duplicate(false);
			trace("DIRECT resource " + copy.getClass() + " " + (copy !== material));
			var missing:Color = null;
			try {
				batch.setInstanceCustomData(0, missing);
			} catch (wrong:Error) {
				trace("DIRECT argument " + wrong.name + " " + wrong.errorID + " " + wrong.message);
			}
		}
	}
}
