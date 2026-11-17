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
		// A vector of ints is what Godot's array of 32-bit integers holds, and goes to it as it is; a vector
		// of uints goes number by number, each of which must fit.
		public function wholes(target:NavigationPolygon, keeper:Node):void {
			var many:Vector.<int> = new <int>[7, -1, 2147483647, -2147483648, 0];
			many.push(12);
			many.unshift(-9);
			target.addPolygon(many);
			target.addPolygon(new <uint>[5, 6, 2147483647]);
			target.addPolygon(new Vector.<int>());
			keeper.setMeta("unsigned", new <uint>[4000000000, 1]);
		}
		public function unsignedTooBig(target:NavigationPolygon):void { target.addPolygon(new <uint>[1, 3000000000]); }
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
		// A vector that is added to while it is being shortened: Godot runs the code of an object as the
		// vector lets go of it, and that code calls back in.
		private var counted:Vector.<RefCounted> = new Vector.<RefCounted>();
		public function count(one:RefCounted):int { return counted.push(one); }
		public function grow(a:RefCounted, b:RefCounted):void { counted.push(a, b); }
		public function shorten():String {
			counted.length = 0;
			var out:String = "" + counted.length;
			for (var i:int = 0; i < counted.length; i++) {
				out += counted[i] == null ? " null" : " held";
			}
			return out;
		}
		public function release():int {
			counted = new Vector.<RefCounted>();
			return counted.length;
		}
		// No vector at all is Godot's null. A place that takes anything gets nothing; a packed array
		// can't be nothing, and Godot's refusal is an AS3 error.
		public function nothing(keeper:Node):Boolean {
			var whole:Vector.<int>;
			keeper.setMeta("nothing", whole);
			return whole == null;
		}
		public function noPoints(target:Line2D):int {
			var none:Vector.<Vector2> = null;
			target.points = none;
			return 1;
		}
		// Godot's own objects made with `new`: a node, which the tree then owns, and a resource, which is
		// counted and goes when nothing holds it.
		public function make():int {
			var child:Node2D = new Node2D();
			child.name = "made";
			child.position = new Vector2(3, 4);
			addChild(child);
			var drawn:Line2D = new Line2D();
			drawn.points = new <Vector2>[new Vector2(0, 0), new Vector2(1, 1)];
			child.addChild(drawn);
			held.push(child);
			return getChildCount() * 10 + drawn.getPointCount();
		}
		public function blend():int {
			var ramp:Gradient = new Gradient();
			ramp.offsets = new <Number>[0, 0.5, 0.75, 1];
			return ramp.getPointCount();
		}
		// AS3's Arrays and Godot's: copied element by element, each way.
		public function toGodot(target:Node):void { target.setMeta("list", [1, "two", new Vector2(1, 2), [3]]); }
		public function fromGodot(list:Array):String { return list.length + ":" + list.join(";"); }
		public function asArray(target:Node):int { var got:Array = target.getMeta("list") as Array; return got.length; }
		public function words():String {
			var all:Vector.<Vector2> = new <Vector2>[new Vector2(1, 2), new Vector2(3, 4)];
			return all.join(" ") + "/" + all + "/" + held.length;
		}
	}
}
