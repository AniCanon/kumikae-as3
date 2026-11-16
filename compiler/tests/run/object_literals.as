// Object literals and dynamic properties. Keys go in the order each was first set: the interpreter's
// ruling, since AS3 leaves the order unspecified.
package {
	public class object_literals {
		public function object_literals() {
			var o:Object = { a: 1, "b": "two", 3: true, 0x10: null };
			trace(o.a, o["b"], o[3], o["3"], o[16], o.missing);
			trace(typeof o, typeof o.missing, String(o), o);
			o.c = 2.5;
			o["d e"] = [1, 2];
			o[1.0] = "one";
			o[-0] = "zero";
			trace(o["1"], o[0], o["d e"].length);
			trace(delete o.a, "a" in o, o.hasOwnProperty("b"), o.hasOwnProperty("a"), "toString" in o, o.hasOwnProperty("toString"));
			var keys:String = "";
			for (var k:String in o) {
				keys += k + " ";
			}
			trace(keys);
			var values:String = "";
			for each (var v:* in o) {
				values += v + " ";
			}
			trace(values);

			var made:Object = new Object();
			trace(made, made.x, made == o, made === made, made != o);
			made.x = 1;
			made.x += 4;
			made.x++;
			made["x"] *= 2;
			trace(made.x);

			// A property that holds a function is called as one; Object's own methods are there too.
			made.twice = function (n:Number):Number { return n * 2; };
			trace(made.twice(21), made.hasOwnProperty("twice"), made.toString(), made.valueOf() === made);
			made.toString = function ():String { return "made by hand"; };
			trace(made, String(made), "" + made, made.toString());

			// Deleting the properties while going through them: each is still reached once, or not at all.
			var many:Object = { p: 1, q: 2, r: 3, s: 4 };
			var seen:String = "";
			for (var name:String in many) {
				seen += name;
				delete many.r;
				many.t = 5; // Added on the way: not reached by this loop.
			}
			trace(seen);
			seen = "";
			for (name in many) {
				seen += name;
			}
			trace(seen);

			// A key that is an object is its text.
			var holder:Object = {};
			holder[made] = "by text";
			trace(holder["made by hand"]);

			var nested:Object = { inner: { deep: [10, { leaf: "found" }] } };
			trace(nested.inner.deep[1].leaf, nested["inner"]["deep"][0]);
			var u:* = nested.inner;
			u.added = "yes";
			trace(nested.inner.added, u.deep.length);
		}
	}
}
