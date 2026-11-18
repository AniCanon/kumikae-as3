package {
	import godot.*;

	// Run twice by run_godot.py, by the interpreter and as native code: what each method gives must be the
	// same text. The interpreter asks Godot for every operation on one of its values, so the same text also
	// says that the runtime's own maths (runtime/godot_values.h) is Godot's to the last bit.
	public class Probe extends Node2D {
		private var total:Number = 0;
		private var kept:Vector2 = new Vector2(1.5, -2.25);
		private var log:Array = [];
		private var heard:Array = [];
		[Signal] public var struck:Signal;

		// A function value that gives one of Godot's values or objects, called every way there is, and
		// `arguments` of a function whose parameters are such values.
		private function giveVector():Vector2 {
			return new Vector2(5, 6);
		}

		private function giveNode():Node2D {
			return this;
		}

		private function giveNothing():Node2D {
			return null;
		}

		private function echo(at:Vector2, node:Node2D, deep:Vector3):String {
			return arguments[0] + " " + (arguments[1] === node) + " " + arguments[2] + " " + arguments.length;
		}

		public function given():String {
			var out:Array = [];
			var vector:Function = giveVector;
			var untyped:* = vector();
			var typed:Vector2 = vector();
			out.push(untyped, typed.x, vector.call(null));
			var node:Function = giveNode;
			var found:* = node();
			var same:Node2D = node();
			out.push(found === this, same === this, node.call(null) === this);
			var nothing:Function = giveNothing;
			out.push(nothing(), nothing() === null);
			out.push(echo(new Vector2(1, 2), this, new Vector3(1, 2, 3)));
			return out.join("|");
		}

		// An error raised by a listener while Godot runs a method this function called on bare values: it
		// comes out of that call, to this function's catch, after its finally.
		private var raising:Boolean = false;

		private function onShown():void {
			if (raising) {
				throw new Error("in listener");
			}
		}

		public function carried():String {
			var said:String = "";
			visibilityChanged.add(onShown);
			raising = true;
			try {
				try {
					hide();
					said += "after-hide ";
				} finally {
					said += "finally ";
				}
			} catch (e:Error) {
				said += "caught " + e.message;
			}
			raising = false;
			show();
			return said + " end " + visible;
		}

		// Each of Godot's value types that has parts: made, its parts read and written, its operators, a
		// method or two, a constant. Native code holds each in a local as the bytes Godot holds, and asks
		// Godot's own code for the type with nothing between, or does the same sums itself.
		public function values():String {
			var out:Array = [];
			var a2:Vector2 = new Vector2(0.1, -7.3);
			var b2:Vector2 = new Vector2(3.3, 1e-3);
			out.push(a2.dot(b2), a2.distanceTo(b2), a2.angle(), a2.lerp(b2, 0.3), -a2, a2 * b2, a2 == b2, a2 != b2, Vector2.ONE, a2.abs());
			var i2:Vector2i = new Vector2i(7, -3);
			var j2:Vector2i = new Vector2i(2, 5);
			i2.x += 4;
			out.push(i2 + j2, i2 - j2, i2 * j2, i2 * 3, i2.x, i2.y, i2 == j2, i2.lengthSquared());
			var a3:Vector3 = new Vector3(0.1, 0.7, -3.3);
			var b3:Vector3 = new Vector3(12345.678, -0.000123, 1 / 3);
			a3.z += 0.25;
			out.push(a3 + b3, a3 - b3, a3 * 0.37, a3 / 3.3, a3 * b3, a3.cross(b3), a3.dot(b3), a3.length(), a3.normalized(), a3.x, a3.y, a3.z);
			out.push(Vector3.UP, a3.lerp(b3, 0.25), a3.distanceTo(b3), -a3, a3 == a3, a3 != b3);
			var triples:Array = [0.1, 0.7, -3.3, 1e-20, 12345.678, -0.000123, 0, 0, 0, 1e19, 1e19, -1e19, 3, 4, 12, 1 / 3, -2 / 3, 1 / 7];
			for (var t:int = 0; t < triples.length; t += 3) {
				var p3:Vector3 = new Vector3(triples[t], triples[t + 1], triples[t + 2]);
				var q3:Vector3 = new Vector3(triples[t + 2] * 1.7, triples[t] - 0.3, triples[t + 1] / 0.9);
				out.push(p3 + q3, p3 - q3, p3 * 0.37, p3 / 3.3, p3.dot(q3), p3.cross(q3), p3.length(), p3.normalized(), p3.normalized().length());
				out.push(new Vector2(triples[t], triples[t + 1]).dot(new Vector2(triples[t + 2], 0.3)));
			}
			var i3:Vector3i = new Vector3i(1, -2, 3);
			out.push(i3 + i3, i3 * 4, i3.z, i3 == new Vector3i(1, -2, 3));
			var a4:Vector4 = new Vector4(0.5, 1.5, -2.5, 1e-7);
			var b4:Vector4 = new Vector4(4, 3, 2, 1);
			out.push(a4 + b4, a4 * 0.1, a4.dot(b4), a4.length(), a4.w, new Vector4i(1, 2, 3, 4) + new Vector4i(4, 3, 2, 1));
			var tint:Color = new Color(0.1, 0.2, 0.3, 0.4);
			var plain:Color = new Color(1, 0.5, 0.25);
			tint.g = 0.9;
			out.push(tint + plain, tint * 0.5, tint.lerp(plain, 0.3), tint.r, tint.g, tint.b, tint.a, plain.a, tint == plain, Color.WHITE);
			var box:Rect2 = new Rect2(1.5, 2.5, 10, 20);
			var other:Rect2 = new Rect2(new Vector2(5, 5), new Vector2(2.5, 100));
			out.push(box.position, box.size, box.end, box.hasPoint(a2), box.hasPoint(new Vector2(2, 3)), box.grow(0.5), box.intersection(other),
					box.getArea(), box == other, new Rect2i(1, 2, 3, 4).size);
			var turn:Quaternion = new Quaternion(0.1, 0.2, 0.3, 0.9);
			var unit:Quaternion = turn.normalized();
			out.push(unit, unit.length(), unit * unit, unit.x, unit.w, unit * a3);
			var frame:Basis = new Basis();
			var spun:Basis = frame.rotated(new Vector3(0, 1, 0), 0.5);
			out.push(spun, spun * a3, spun.x, spun.determinant(), spun * spun, spun == frame);
			var flat:Transform2D = new Transform2D();
			flat.origin = new Vector2(3, 4);
			out.push(flat, flat * a2, flat.origin, flat.rotated(0.25), flat.x);
			var deep:Transform3D = new Transform3D();
			deep.origin = b3;
			out.push(deep * a3, deep.origin, deep.basis, deep.translated(a3).origin);
			var room:AABB = new AABB(new Vector3(0, 0, 0), new Vector3(1, 2, 3));
			out.push(room.hasPoint(new Vector3(0.5, 1, 2)), room.hasPoint(b3), room.size, room.end, room.getVolume());
			var wall:Plane = new Plane(0, 1, 0, 2.5);
			out.push(wall.distanceTo(a3), wall.normal, wall.d, wall.isPointOver(a3));
			var lens:Projection = new Projection();
			out.push(lens.x, lens.determinant(), lens * a4);
			out.push(new Vector2(), new Vector3i(), new Color(), new Quaternion(), new Plane(), new Rect2(), new AABB(), new Vector4());
			return out.join(" ");
		}

		// A value with parts that is a field of the script, read only to be used at once (an operator, a
		// method, a part): native code reads it out of the field's own handle, with none made for the read.
		// A field that holds none is AS3's null error where it is used, a field written between its read and
		// its use is used as it was read, and a variable given the field keeps its own.
		private var none:Vector2;
		private var turned:Transform2D = new Transform2D(0.5, new Vector2(3, 4));
		private var tint:Color = new Color(0.1, 0.2, 0.3, 0.4);

		private function swap():Number {
			kept = new Vector2(100, 100);
			return 2;
		}

		public function fields():String {
			var out:Array = [];
			out.push(kept * 2.5, kept + kept, kept.length(), kept.x, kept.y, kept.dot(kept), -kept, kept == kept, kept.lerp(kept * 3, 0.5));
			for (var i:int = 0; i < 3; i++) {
				var p:Vector2 = new Vector2(i, 1);
				p = p + kept * 0.5;
				out.push(p);
			}
			out.push(turned * kept, turned.origin, turned.rotated(0.25), tint * 0.5, tint.g, tint.lerp(tint, 0.5));
			var before:Vector2 = kept;
			out.push(kept * swap(), before, kept * 2);
			kept = new Vector2(1.5, -2.25);
			out.push(before, none * 2, none.x);
			none = null;
			try {
				out.push(none * 2);
			} catch (e:TypeError) {
				out.push(e.message);
			}
			try {
				out.push(none.x);
			} catch (e:TypeError) {
				out.push(e.message);
			}
			try {
				out.push(none.length());
			} catch (e:TypeError) {
				out.push(e.message);
			}
			try {
				out.push(kept + none);
			} catch (e:TypeError) {
				out.push(e.message);
			}
			return out.join(" ");
		}

		// Every operation generated code does inline on a Vector2, on values a float can't hold exactly,
		// on large and small ones, and on the zero vector.
		public function vectors():String {
			var out:Array = [];
			var pairs:Array = [0.1, 0.7, -3.3, 1e-20, 12345.678, -0.000123, 0, 0, 1e19, 1e19, 3, 4, 1 / 3, -2 / 3];
			for (var i:int = 0; i < pairs.length; i += 2) {
				var a:Vector2 = new Vector2(pairs[i], pairs[i + 1]);
				var b:Vector2 = new Vector2(pairs[i + 1] * 1.7, pairs[i] - 0.3);
				var sum:Vector2 = a + b;
				var less:Vector2 = a - b;
				var scaled:Vector2 = a * 0.37;
				var cut:Vector2 = a / 3.3;
				var unit:Vector2 = a.normalized();
				out.push(sum.x, sum.y, less.x, less.y, scaled.x, scaled.y, cut.x, cut.y, unit.x, unit.y, a.length(), unit.length());
			}
			var walk:Vector2 = new Vector2(0, 0);
			var step:Vector2 = new Vector2(0.5, 0.25);
			for (var n:int = 0; n < 1000; n++) {
				walk = walk + step * 0.5;
				walk = walk.normalized() * 3.0;
			}
			out.push(walk.x, walk.y, new Vector2(2, 0).x);
			var part:Vector2 = new Vector2(1, 2);
			part.x = 0.1;
			part.y = part.x * 3 - 7.7;
			part.x += part.y;
			out.push(part.x, part.y);
			return out.join(" ");
		}

		// Properties and methods of the script's own Godot object and of a singleton, called on bare values,
		// and what generated code leaves to the library: a String property, a value kept in a field, a
		// value held untyped.
		public function engine():String {
			var out:Array = [];
			position = new Vector2(3.25, -7.5);
			position = position + new Vector2(0.125, 0.5);
			rotation = 0.75;
			rotation += 0.001;
			out.push(position.x, position.y, rotation, isInsideTree(), Input.isActionPressed("ui_accept"), getChildCount());
			scale = scale * 2.0;
			visible = !visible;
			out.push(scale.x, visible, zIndex);
			name = "probe";
			out.push(name, kept.x, kept.y);
			kept = kept + position;
			var any:* = kept;
			var back:Vector2 = any;
			out.push(back.x, back.y, String(any), kept.length());
			var child:Node2D = new Node2D();
			addChild(child);
			child.position = new Vector2(9, 8);
			out.push(getChildCount(), child.position.x + child.position.y, child.getParent() == this);
			child.queueFree();
			return out.join(" ");
		}

		// A field that holds one of Godot's objects, read again and again to call it. Native code keeps the
		// handle a register has while the field still holds the same object: so the field is given another
		// object between two reads, one Godot counts references of and one it does not, an object is freed
		// and another put in its place, a listener empties the field while Godot is still in the call that
		// was made on what it held, and the field holds none.
		private var query:PhysicsRayQueryParameters2D;
		private var other:Node2D;
		private var circle:CircleShape2D;
		private var emptied:int = 0;

		private function onCircleChanged():void {
			circle = null;
			emptied++;
		}

		public function fieldObjects():String {
			var out:Array = [];
			query = new PhysicsRayQueryParameters2D();
			var first:PhysicsRayQueryParameters2D = query;
			for (var i:int = 0; i < 6; i++) {
				if (i == 3) {
					query = new PhysicsRayQueryParameters2D();
				}
				query.from = new Vector2(i, i * 2);
				query.collisionMask = i + 1;
				out.push(query.from.x + query.collisionMask);
			}
			out.push(first.from.x, first.collisionMask, query.from.x, query.collisionMask);
			other = new Node2D();
			for (i = 0; i < 5; i++) {
				if (i == 2) {
					other.free();
					other = new Node2D();
				}
				other.position = new Vector2(i, 0);
				other.rotation += 1;
				out.push(other.position.x + other.rotation);
			}
			other.free();
			try {
				other.position = new Vector2(9, 9);
				out.push("no error");
			} catch (e:Error) {
				out.push("freed: " + e.message);
			}
			other = null;
			circle = new CircleShape2D();
			circle.changed.add(onCircleChanged);
			circle.radius = 12;
			out.push(circle === null, emptied);
			try {
				circle.radius = 3;
				out.push("no error");
			} catch (none:Error) {
				out.push("none: " + none.message);
			}
			query = null;
			return out.join("|");
		}

		// What Godot gives in a handle (a Dictionary, a RID) from a method called on bare values,
		// methods of such a value called where the handle keeps it, and one of Godot's objects handed to a
		// method or a property that takes exactly its class: an object, none, and one Godot has freed.
		public function inHandles():String {
			var out:Array = [];
			var date:GodotDictionary = Time.getDateDictFromUnixTime(86400 * 366);
			out.push(date.isEmpty(), date.size(), date["year"], date["month"], date["day"]);
			Time.getDateDictFromUnixTime(5);
			for (var i:int = 0; i < 3; i++) {
				date = Time.getDateDictFromUnixTime(86400 * 31 * i);
				out.push(date["month"]);
			}
			var empty:GodotDictionary = new GodotDictionary();
			out.push(empty.isEmpty(), empty.size());
			empty["one"] = 1;
			out.push(empty.isEmpty(), empty.size());
			var none:GodotDictionary = null;
			try {
				out.push(none.isEmpty());
			} catch (e:Error) {
				out.push("none: " + e.message);
			}
			// An element of one of Godot's Dictionaries read into a value with parts: the element as that type
			// at once, and what the two instructions say of one that is not there, or is something else.
			var places:GodotDictionary = new GodotDictionary();
			places["at"] = new Vector2(3, 4);
			places["deep"] = new Vector3(1, 2, 3);
			places["count"] = 5;
			for (i = 0; i < 3; i++) {
				var at:Vector2 = places["at"];
				out.push(at.x + at.y + i);
				places["at"] = at * 2.0;
			}
			try {
				var miss:Vector2 = places["nowhere"];
				out.push(miss);
			} catch (missing:Error) {
				out.push("missing: " + missing.message);
			}
			try {
				var wrong:Vector2 = places["deep"];
				out.push(wrong);
			} catch (other:Error) {
				out.push("other: " + other.message);
			}
			try {
				var number:Vector2 = places["count"];
				out.push(number);
			} catch (count:Error) {
				out.push("count: " + count.message);
			}
			try {
				var from:Vector2 = none["at"];
				out.push(from);
			} catch (nothing:Error) {
				out.push("nothing: " + nothing.message);
			}
			var item:RID = getCanvasItem();
			out.push(item.isValid(), item.getID() > 0, new RID().isValid());
			var holder:CollisionShape2D = new CollisionShape2D();
			var round:Shape2D = new CircleShape2D();
			holder.shape = round;
			out.push(holder.shape == round);
			round = null;
			holder.shape = round;
			out.push(holder.shape == null);
			holder.free();
			var kid:Node = new Node();
			out.push(isAncestorOf(kid));
			addChild(kid);
			out.push(isAncestorOf(kid), kid.getIndex() >= 0);
			removeChild(kid);
			out.push(isAncestorOf(kid));
			// One Godot has freed is handed over as none, as the library hands it.
			var held:CollisionShape2D = new CollisionShape2D();
			var gone:Node = new Node();
			held.addChild(gone);
			out.push(held.isAncestorOf(gone), held.getChildCount());
			gone.free();
			out.push(held.getChildCount(), held.isAncestorOf(gone));
			held.free();
			kid.free();
			return out.join("|");
		}

		// A method of one of Godot's Arrays called where the handle keeps the Array, which lets go of what the
		// Array held: an object whose end calls back into the script (probe.gd's Dying), where the script
		// throws. The error comes out of the call that ended the object, as it does under the interpreter.
		private var mortal:GodotArray = new GodotArray();
		private var died:int = 0;

		public function dies():void {
			died++;
			throw new Error("dying");
		}

		public function holds(one:RefCounted):int {
			mortal.append(one);
			return mortal.size();
		}

		public function clears():String {
			var said:String = "";
			try {
				mortal.clear();
				said += "cleared ";
			} catch (e:Error) {
				said += "caught " + e.message + " ";
			}
			return said + died + " " + mortal.size();
		}

		// The script's own code, with nothing of Godot's: arithmetic, Strings, an Array, a function value,
		// an error caught.
		public function script(rounds:int):String {
			var checksum:int = 0;
			for (var i:int = 0; i < rounds; i++) {
				total += scaled(i, 0.5);
				if ((i & 7) == 0) {
					checksum ^= bump(i);
				}
			}
			var twice:Function = function (x:Number):Number { return x * 2; };
			log.push(twice(total), "round " + rounds);
			try {
				var nothing:Probe = null;
				nothing.total = 1;
			} catch (e:TypeError) {
				log.push(e.errorID);
			}
			return checksum + " " + total + " " + log.join("|");
		}

		// Two listeners that both fail: the first one's error comes out of emit(), where it is caught; Godot
		// goes on to the second all the same, whose error is reported as it ends.
		public function listeners():String {
			struck.add(first);
			struck.add(second);
			try {
				struck.emit(3);
			} catch (e:Error) {
				heard.push("caught " + e.message);
			}
			return heard.join("|");
		}

		private function first(n:int):void {
			heard.push("first " + n);
			throw new Error("from the first");
		}

		private function second(n:int):void {
			heard.push("second " + n);
			throw new Error("from the second");
		}

		// The script's own Godot object is freed in the middle of a method (the killer does it, when asked),
		// and the method goes on using it: what is asked of it from then on is asked of nothing. From
		// _process, because Godot refuses to free an object while a call it was asked to make is running.
		public var killer:Node;

		override public function _process(delta:Number):void {
			if (killer == null) {
				return;
			}
			var asked:Node = killer;
			killer = null;
			var out:Array = [];
			position = new Vector2(1, 2);
			out.push(position.x);
			asked.call("kill");
			try {
				out.push(position.x);
			} catch (e:Error) {
				out.push("read: " + e.message);
			}
			try {
				position = new Vector2(3, 4);
				out.push("moved");
			} catch (e:Error) {
				out.push("move: " + e.message);
			}
			try {
				rotation = 1;
				out.push("written");
			} catch (e:Error) {
				out.push("write: " + e.message);
			}
			try {
				out.push(isInsideTree());
			} catch (e:Error) {
				out.push("call: " + e.message);
			}
			asked.call("report", out.join("|"));
		}

		private var mute:Boolean = false;

		public function toString():String {
			if (mute) {
				throw new Error("nothing to say");
			}
			return "the probe";
		}

		// Godot puts one of its objects into words by running its script: an error there comes out of the
		// join() that asked, where it is caught.
		public function speaks():String {
			var nodes:Array = [getParent().getChild(getIndex())];
			var said:String = nodes.join("+");
			mute = true;
			try {
				said += nodes.join("+");
			} catch (e:Error) {
				said += " caught " + e.message;
			}
			mute = false;
			return said + " " + nodes.join("+");
		}

		// One of Godot's objects that has been freed, used again: AS3's error, with its line, however native
		// code holds the object. Freed between two uses in one function, inside a function called, by a
		// listener the engine ran for a call, in the loop that moves a vector's nodes, and at a frame's end;
		// and one a function called took out of the vector while a variable held it, which is alive still.
		private var crowd:Vector.<Node2D> = new Vector.<Node2D>();
		private var doomed:Node2D;
		private var queued:Node2D;

		private function freeing(node:Node2D):void {
			node.free();
		}

		private function scatter():void {
			crowd.length = 0;
		}

		private function onDoom():void {
			if (doomed) {
				doomed.free();
			}
		}

		private function tell(node:Node2D):String {
			try {
				node.position = new Vector2(1, 2);
				return "" + node.position;
			} catch (e:Error) {
				return e.message;
			}
			return "never";
		}

		public function gone():String {
			var out:Array = [];
			var b:Node2D = new Node2D();
			b.position = new Vector2(3, 4);
			var p:Vector2 = b.position;
			b.free();
			try {
				b.position = p;
				out.push("moved");
			} catch (e:Error) {
				out.push(e.message);
			}
			var c:Node2D = new Node2D();
			c.position = p;
			freeing(c);
			try {
				out.push(c.position);
			} catch (e:Error) {
				out.push(e.message);
			}
			var bell:Node2D = new Node2D();
			doomed = new Node2D();
			var d:Node2D = doomed;
			bell.visibilityChanged.add(onDoom);
			d.position = p;
			bell.hide();
			try {
				d.position = p;
				out.push("moved");
			} catch (e:Error) {
				out.push(e.message);
			}
			doomed = null;
			bell.free();
			for (var n:int = 0; n < 4; n++) {
				crowd.push(new Node2D());
			}
			for (var i:int = 0; i < crowd.length; i++) {
				var child:Node2D = crowd[i];
				try {
					var at:Vector2 = child.position;
					at.x += i + 1;
					child.position = at;
					out.push(child.position.x);
					if (i == 1) {
						crowd[2].free();
					}
				} catch (e:Error) {
					out.push(i + ": " + e.message);
				}
			}
			var first:Node2D = crowd[0];
			var second:Node2D = crowd[1];
			var last:Node2D = crowd[3];
			scatter();
			out.push(tell(first), tell(second), first == second, crowd.length);
			second.free();
			last.free();
			freeing(first);
			out.push(tell(first), tell(second), tell(last), first == null);
			return out.join(" | ");
		}

		public function goneUncaught():Number {
			var node:Node2D = new Node2D();
			node.free();
			return node.position.x;
		}

		public function queue():String {
			queued = new Node2D();
			addChild(queued);
			queued.position = new Vector2(5, 6);
			queued.queueFree();
			return "" + queued.position;
		}

		public function afterQueue():String {
			try {
				queued.position = new Vector2(7, 8);
				return "" + queued.position;
			} catch (e:Error) {
				return e.message;
			}
			return "never";
		}

		// `is`, `as` and a cast with one of Godot's value types, on values held every way.
		public function kinds(value:*):String {
			var held:Object = value;
			var out:String = (value is Vector2) + " " + (held is Color) + " " + (kept is Vector2) + " " + (kept is Color) + " " + (value as Vector2) + " " + (held as Color);
			try {
				out += " " + Vector2(value);
			} catch (e:TypeError) {
				out += " " + e.errorID;
			}
			return out;
		}

		// An error nothing catches: Godot is told, with the trace, and carries on.
		public function fail(depth:int):int {
			if (depth == 0) {
				var none:Array = null;
				return none.length;
			}
			return fail(depth - 1) + 1;
		}

		private function scaled(value:int, by:Number):Number {
			return value * by + 1;
		}

		private function bump(value:int):int {
			return value % 97 + log.length;
		}
	}
}
