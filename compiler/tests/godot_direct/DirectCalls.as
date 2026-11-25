package {
	import godot.*;

	public class DirectCalls extends Node2D {
		private var mesh:MultiMesh;
		private var picture:ImageTexture;
		private var canvas:RID;
		private var ink:RID;
		private var batches:Vector.<MultiMesh> = new Vector.<MultiMesh>();
		private var shelf:Vector.<Resource> = new Vector.<Resource>();
		private var heard:int = 0;

		override public function _process(delta:Number):void {
			var batch:MultiMesh = mesh;
			var item:RID = canvas;
			var texture:RID = ink;
			for (var i:int = 0; i < 16; ++i) {
				RenderingServer.canvasItemAddTextureRectRegion(item, new Rect2(i, 0, 1, 1), texture, new Rect2(0, 0, 1, 1));
				var point:Vector2 = new Vector2(i, -i);
				var transform:Transform2D = new Transform2D(delta, new Vector2(2, 3), 0, point);
				var color:Color = new Color(i / 16, 0.5, 0, 1);
				batch.setInstanceTransform2D(0, transform);
				batch.setInstanceCustomData(0, color);
			}
			// Each read of an element hands its register a handle; one that holds the same object keeps its own.
			for (var k:int = 0; k < 16; ++k) {
				batches[k >> 3].setInstanceTransform2D(0, new Transform2D());
				batches[k >> 3].setInstanceCustomData(0, new Color(0, 0, 0, 1));
			}
		}

		private function onShelf():void {
			heard++;
			if (heard == 2) {
				shelf.length = 0;
			}
		}

		// Each object read from a vector, whether the next read gives another or the same, gets its own value:
		// a register that keeps its handle keeps it only for the object it was given.
		private function marked():void {
			for (var i:int = 0; i < 4; ++i) {
				batches[i & 1].instanceCount = (i & 1) + 1;
			}
			trace("DIRECT alternating " + batches[0].instanceCount + " " + batches[1].instanceCount);
			for (var k:int = 0; k < 16; ++k) {
				batches[k >> 3].instanceCount = (k >> 3) + 3;
			}
			trace("DIRECT runs " + batches[0].instanceCount + " " + batches[1].instanceCount);
		}

		// A listener empties the vector while Godot runs a method of the element read from it: the register
		// read last holds the object still.
		private function shelved():void {
			var made:Resource = new Resource();
			made.changed.add(onShelf);
			shelf.push(made);
			made = null;
			for (var i:int = 0; i < 2; ++i) {
				shelf[0].emitChanged();
			}
			trace("DIRECT shelf " + heard + " " + shelf.length);
		}

		private function classOf(node:Node):String {
			try {
				return node.getClass();
			} catch (e:Error) {
				return e.name + " " + e.errorID + " " + e.message;
			}
		}

		// Defaults Godot declares as values are filled where the call is prepared, for a singleton's method
		// and for a typed receiver's.
		private function defaults():void {
			picture = ImageTexture.createFromImage(Image.createEmpty(4, 4, false, Image.FORMAT_RGBA8));
			canvas = getCanvasItem();
			ink = picture.getRID();
			var body:RID = PhysicsServer2D.bodyCreate();
			var shape:RID = PhysicsServer2D.circleShapeCreate();
			PhysicsServer2D.bodyAddShape(body, shape);
			trace("DIRECT default " + PhysicsServer2D.bodyGetShapeTransform(body, 0));
			var none:Rect2 = null;
			try {
				RenderingServer.canvasItemAddTextureRectRegion(canvas, none, ink, new Rect2(0, 0, 1, 1));
				trace("DIRECT null value accepted");
			} catch (wrong:Error) {
				trace("DIRECT null value " + wrong.name + " " + wrong.errorID + " " + wrong.message);
			}
			PhysicsServer2D.freeRID(body);
			PhysicsServer2D.freeRID(shape);
			var atlas:TileSetAtlasSource = new TileSetAtlasSource();
			atlas.texture = picture;
			atlas.textureRegionSize = new Vector2i(1, 1);
			atlas.createTile(new Vector2i(2, 1));
			trace("DIRECT tile " + atlas.getTileSizeInAtlas(new Vector2i(2, 1)));
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
			defaults();
			batches.push(new MultiMesh());
			batches.push(new MultiMesh());
			for each (var each:MultiMesh in batches) {
				each.useCustomData = true;
				each.instanceCount = 1;
			}
			marked();
			shelved();
		}
	}
}
