package {
	import godot.*;

	// A vector of Vector2s or of Vector2is holds each value in its place, as a C# array of structs does: never
	// a null, a copy read out and a copy stored. places.gd says what GDScript gives for the same, and
	// run_godot.py's native probe runs every method here as native code too, held to the interpreter.
	public class Places extends Node2D {
		private var kept:Vector.<Vector2> = new <Vector2>[new Vector2(1, 1)];
		private var cells:Vector.<int>;
		[Signal] public var shortened:Signal;

		// Made with a length, made longer, added to at the end, and each place read and written.
		public function grown():String {
			var v:Vector.<Vector2> = new Vector.<Vector2>(2);
			v.length = 3;
			var count:uint = v.push(new Vector2(1, 2));
			v[4] = new Vector2(3, 4);
			var first:Vector2 = v[0];
			first.x = 9;
			v[1] = first;
			v[2] = v[3] * 2 + v[4];
			for (var i:int = 0; i < 40; i++) {
				v.push(new Vector2(i, -i));
			}
			return count + " " + v.length + " " + v.slice(0, 6).join(" ") + " " + v[44] + " " + v[0].x + " " + (v[0] == v[1]) + " " + (v[1] == first) + " " +
					(v[0] == null) + " " + (v[1] != null);
		}

		// No value is none a place can hold: an error, with the vector as it was. (`null` itself written there
		// is refused by the checker; here it is in a variable.)
		public function refused():String {
			var v:Vector.<Vector2> = new <Vector2>[new Vector2(1, 2), new Vector2(3, 4)];
			var none:Vector2 = null;
			var said:Array = [];
			try { v[0] = none; } catch (a:TypeError) { said.push(a.message); }
			try { v[2] = none; } catch (b:TypeError) { said.push(v.length); }
			try { v.push(none); } catch (c:TypeError) { said.push(v.length); }
			try { v.push(v[0], none); } catch (d:TypeError) { said.push(v.length); }
			try { v.unshift(none); } catch (e:TypeError) { said.push(v.length); }
			try { v.insertAt(1, none); } catch (f:TypeError) { said.push(v.length); }
			try { v.splice(0, 1, none); } catch (g:TypeError) { said.push(v.length); }
			try { var w:Vector.<Vector2> = new <Vector2>[new Vector2(5, 5), none]; said.push(w.length); } catch (k:TypeError) { said.push("made"); }
			try { said.push(v[2]); } catch (m:RangeError) { said.push(m.message); }
			try { v[3] = v[0]; } catch (n:RangeError) { said.push(n.message); }
			var fixed:Vector.<Vector2> = new Vector.<Vector2>(1, true);
			try { fixed.push(v[0]); } catch (p:RangeError) { said.push(p.message); }
			fixed[0] = v[1];
			said.push(fixed, v);
			return said.join(";");
		}

		private function byX(a:Vector2, b:Vector2):Number {
			return a.x - b.x;
		}

		// Every method of a vector, on one whose places hold the values.
		public function methods():String {
			var v:Vector.<Vector2> = new <Vector2>[new Vector2(3, 0), new Vector2(1, 1), new Vector2(2, 2)];
			var said:Array = [];
			said.push(v.pop(), v.shift(), v.length, v.unshift(new Vector2(7, 7), new Vector2(8, 8)));
			v.insertAt(1, new Vector2(5, 5));
			said.push(v.removeAt(0), String(v));
			var taken:Vector.<Vector2> = v.splice(1, 1, new Vector2(4, 4), new Vector2(6, 6), new Vector2(0, 9));
			said.push(taken, taken.length, String(v));
			said.push(v.slice(1, 3), v.concat(taken, kept), String(v.reverse()));
			said.push(v.indexOf(new Vector2(4, 4)), v.lastIndexOf(new Vector2(4, 4)), v.indexOf(new Vector2(4, 5)), v.indexOf(v[3], 1));
			said.push(String(v.sort(byX)));
			var total:Vector2 = new Vector2(0, 0);
			for each (var each:Vector2 in v) {
				total += each;
			}
			var keys:String = "";
			for (var key:String in v) {
				keys += key;
			}
			said.push(total, keys);
			said.push(v.map(function(item:Vector2, index:int, all:Vector.<Vector2>):Vector2 { return item * 2; }));
			said.push(v.filter(function(item:Vector2, index:int, all:Vector.<Vector2>):Boolean { return item.x > 4; }));
			said.push(v.some(function(item:Vector2, index:int, all:Vector.<Vector2>):Boolean { return item.y == 9; }),
					v.every(function(item:Vector2, index:int, all:Vector.<Vector2>):Boolean { return item.y == 9; }));
			var empty:Vector.<Vector2> = new Vector.<Vector2>();
			said.push(empty.pop() == null, empty.shift() == null, empty.length, empty.join("-") == "", v.toString() == String(v));
			kept.push(v[0]);
			var moved:Vector2 = kept[0];
			moved.x += 1;
			kept[0] = moved;
			said.push(String(kept), kept.length);
			kept.length = 1;
			return said.join(";");
		}

		private function sumOf(v:Vector.<Vector2>, count:int):Vector2 {
			var total:Vector2 = new Vector2(0, 0);
			for (var i:int = 0; i < count; i++) { total += v[i]; }
			return total;
		}

		private function setEach(v:Vector.<Vector2>, count:int, value:Vector2):void {
			for (var i:int = 0; i < count; i += 2) { v[i] = value; }
		}

		private function onShortened():void { cells.length = cells.length - 2; }

		private var rung:int;
		private function ring(count:int):void {
			for (var i:int = 0; i < count; i++) {
				rung += cells[i];
				shortened.emit();
			}
		}

		// A loop that goes past the end of a vector of places, reading and storing; and one whose vector a
		// listener Godot calls each round makes shorter.
		public function ends():String {
			var v:Vector.<Vector2> = new <Vector2>[new Vector2(1, 2), new Vector2(3, 4), new Vector2(5, 6)];
			var said:Array = [sumOf(v, 3)];
			try { said.push(sumOf(v, 5)); } catch (a:RangeError) { said.push(a.message); }
			try { setEach(v, 6, new Vector2(7, 7)); } catch (b:RangeError) { said.push(v.length, v, b.message); }
			cells = new <int>[1, 2, 3, 4, 5, 6, 7, 8];
			shortened.add(onShortened);
			rung = 0;
			try { ring(8); } catch (c:RangeError) { said.push(rung, cells.length, c.message); }
			shortened.remove(onShortened);
			return said.join(";");
		}

		// Handed to Godot: the places as a packed array's elements, or each as a value of an Array.
		public function given(line:Line2D, keeper:Node):int {
			var v:Vector.<Vector2> = new Vector.<Vector2>(1);
			for (var i:int = 0; i < 5; i++) {
				v.push(new Vector2(i, i * 0.5));
			}
			v.removeAt(2);
			line.points = v;
			keeper.setMeta("points", v);
			keeper.setMeta("nested", [v.slice(0, 2), new <Vector2i>[new Vector2i(1, -2)]]);
			keeper.setMeta("none", new Vector.<Vector2>());
			var cells:Vector.<Vector2i> = new Vector.<Vector2i>(1);
			cells.push(new Vector2i(3, -4), new Vector2i(2147483647, -2147483648));
			cells[0] = cells[1] + cells[1];
			keeper.setMeta("cells", cells);
			line.points = new Vector.<Vector2>();
			var after:int = line.getPointCount();
			line.points = v;
			return after * 100 + line.getPointCount() * 10 + Geometry2D.convexHull(v).size();
		}

		// A vector of a larger value type holds a handle on each, as before: a place may be null, and reads so.
		public function wider(shade:Gradient):String {
			var colors:Vector.<Color> = new Vector.<Color>(1);
			var count:uint = colors.push(new Color(1, 0, 0.5));
			colors.push(null);
			colors[3] = colors[1];
			colors[1] = new Color(0, 1, 0);
			var first:Color = colors[0];
			first.r = 0.25;
			var said:Array = [count, colors.length, colors[0], colors[1], colors[2] == null, colors[3], first];
			colors[2] = first;
			said.push(colors.pop(), colors.indexOf(first), colors.length);
			shade.colors = colors;
			colors[0] = null;
			said.push(colors[0] == null, colors.length);
			var deep:Vector.<Vector3> = new <Vector3>[new Vector3(1, 2, 3), null];
			for (var i:int = 0; i < 20; i++) {
				deep.push(new Vector3(i, 0, -i));
			}
			deep[1] = deep[0] + deep[21];
			said.push(deep.length, deep[1], deep.shift(), deep[0] == deep[1], String(deep.slice(19)));
			return said.join(";");
		}

		// One part of an element changed where it is: the element is read, changed, and written back into the
		// vector, its index worked out once. A vector that holds the values themselves, and one of a larger
		// type that holds a reference to each.
		public function parts():String {
			var kept:Vector.<Vector2> = new <Vector2>[new Vector2(1, 2), new Vector2(3, 4)];
			kept[0].x = 9;
			kept[0].y += 1;
			kept[1].x++;
			--kept[1].y;
			var at:int = 0;
			kept[at++].x *= 2;
			kept[1][0] = 7;
			var cells:Vector.<Vector2i> = new Vector.<Vector2i>(1);
			cells[0].x = 5;
			cells[0].y -= 2;
			var deep:Vector.<Vector3> = new <Vector3>[new Vector3(1, 2, 3)];
			deep[0].z = 8;
			deep[0].x += 0.5;
			var tints:Vector.<Color> = new <Color>[new Color(0, 0, 0)];
			tints[0].r = 1;
			tints[0].a -= 0.5;
			var boxes:Vector.<Rect2> = new <Rect2>[new Rect2(0, 0, 1, 1)];
			boxes[0].size.x = 6;
			boxes[0].position += new Vector2(1, 1);
			var said:Array = [kept, at, cells, deep, tints, boxes];
			var copy:Vector2 = kept[0];
			copy.x = -1;
			said.push(kept[0].x);
			try { kept[5].x = 1; } catch (e:RangeError) { said.push(e.errorID); }
			var none:Vector.<Vector3> = new Vector.<Vector3>(1);
			none[0].x = 1;
			said.push(none[0]);
			var holds:Vector.<Vector3> = new <Vector3>[null];
			try { holds[0].x = 1; } catch (missing:Error) { said.push("no value"); }
			return said.join(";");
		}

		// What arrives from Godot, typed and untyped, stored into a place; and what is of another type refused.
		public function taken(line:Line2D, list:Array):String {
			var v:Vector.<Vector2> = new Vector.<Vector2>(2);
			var said:Array = [];
			v[0] = line.getPointPosition(1);
			v.push(line.position, line.getPointPosition(0) + position);
			var loose:* = list[0];
			v[1] = loose;
			var typed:Vector2 = list[0];
			v.push(typed);
			said.push(String(v));
			try { v[0] = list[1]; } catch (a:TypeError) { said.push(a.message); }
			try { typed = list[2]; v.push(typed); } catch (b:TypeError) { said.push(v.length); }
			var copied:Vector.<Vector2> = Vector.<Vector2>([list[0], v[0]]);
			said.push(copied);
			try { said.push(Vector.<Vector2>(list)); } catch (c:TypeError) { said.push(c.message); }
			var untyped:* = v;
			untyped[0] = list[0];
			untyped.push(list[0]);
			said.push(untyped[0], untyped.length, untyped.indexOf(list[0]));
			try { untyped[1] = list[1]; } catch (d:TypeError) { said.push(d.message); }
			try { untyped.push(null); } catch (e:TypeError) { said.push(untyped.length); }
			try { untyped[0] = undefined; } catch (f:TypeError) { said.push(untyped[0]); }
			var cells:Vector.<Vector2i> = new <Vector2i>[new Vector2i(1, 2)];
			try { untyped = cells; untyped[0] = list[0]; } catch (g:TypeError) { said.push(g.message); }
			said.push(cells);
			return said.join(";");
		}
	}
}
