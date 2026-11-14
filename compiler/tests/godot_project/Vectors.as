package {
	import godot.*;

	// AS3's vectors handed to Godot: vectors.gd does the same in GDScript.
	public class Vectors extends Node2D {
		private var held:Vector.<Node2D> = new Vector.<Node2D>();

		public function line(target:Line2D):void {
			var points:Vector.<Vector2> = new <Vector2>[new Vector2(1, 2), new Vector2(3, 4)];
			points.push(new Vector2(5, 6));
			target.points = points;
		}
		public function hull():PackedVector2Array {
			return Geometry2D.convexHull(new <Vector2>[new Vector2(0, 0), new Vector2(4, 0), new Vector2(2, 1), new Vector2(4, 4), new Vector2(0, 4)]);
		}
		public function bytes():String { return Marshalls.rawToBase64(new <int>[72, 105, 255]); }
		public function tooBig():String { return Marshalls.rawToBase64(new <int>[72, 256]); }
		public function negative():String { return Marshalls.rawToBase64(new <int>[-1]); }
		public function empty():String { return Marshalls.rawToBase64(new Vector.<uint>()); }
		public function shade(target:Gradient):void {
			target.offsets = new <Number>[0, 0.25, 1];
			target.colors = new <Color>[new Color(1, 0, 0), new Color(0, 1, 0), new Color(0, 0, 1)];
		}
		public function filter(dialog:FileDialog):void { dialog.filters = new <String>["*.png", "*.as", null]; }
		public function outline(target:NavigationPolygon):void {
			var corners:Vector.<int> = new <int>[0, 1, 2];
			target.addPolygon(corners);
			corners[0] = 3;
			target.addPolygon(corners);
		}
		public function remember(target:Node, a:Node, b:Node):void {
			target.setMeta("whole", new <int>[1, -2, 3]);
			target.setMeta("fractions", new <Number>[0.5, 2]);
			target.setMeta("words", new <String>["a", "b"]);
			target.setMeta("flags", new <Boolean>[true, false]);
			target.setMeta("nodes", new <Node>[a, b, null]);
			target.setMeta("wide", new <Int64>[9007199254740993]);
		}
		// A vector kept in a field, of Godot's objects: the shape of a script that moves its children.
		public function keep(child:Node2D):int { return held.push(child); }
		public function spread():Number {
			var total:Number = 0;
			for (var i:int = 0; i < held.length; i++) {
				held[i].position = new Vector2(i * 10, i);
				total += held[i].position.x;
			}
			for each (var child:Node2D in held) {
				total += child.position.y;
			}
			return total;
		}
		public function forget():int {
			held.length = 0;
			return held.length;
		}
		public function words():String {
			var all:Vector.<Vector2> = new <Vector2>[new Vector2(1, 2), new Vector2(3, 4)];
			return all.join(" ") + "/" + all + "/" + held.length;
		}
	}
}
