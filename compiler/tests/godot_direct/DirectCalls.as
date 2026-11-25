package {
	import godot.*;

	public class DirectCalls extends Node2D {
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
