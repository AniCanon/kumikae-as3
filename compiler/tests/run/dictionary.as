// flash.utils.Dictionary: an object is a key by its identity; a String, a number or a Boolean by its
// text, as on an Object (so 1 and "1" are one key). Keys go in the order each was first set.
package {
	import flash.utils.Dictionary;

	public class dictionary {
		public function dictionary() {
			var d:Dictionary = new Dictionary();
			var a:Key = new Key("a");
			var b:Key = new Key("b");
			var twin:Key = new Key("a"); // Says the same as `a`, and is another key.
			d[a] = 1;
			d[b] = "bee";
			d[twin] = 3;
			d["a"] = "text";
			d[1] = "one";
			d["1"] = "uno";
			d[true] = "yes";
			trace(d[a], d[b], d[twin], d["a"], d[1], d["true"], d[new Key("a")]);
			trace(typeof d, d, a in d, new Key("b") in d, d.hasOwnProperty(b), d.hasOwnProperty("1"));
			var keys:String = "";
			for (var k:* in d) {
				keys += (k is Key ? "key " + k.name : typeof k + " " + k) + ", ";
			}
			trace(keys);
			trace(delete d[b], b in d, delete d[b]);
			var values:String = "";
			for each (var v:* in d) {
				values += v + " ";
			}
			trace(values);
			d.size = 2;
			trace(d.size, d["size"]);

			// Untyped, and as an Object: the same container.
			var u:* = d;
			trace(u[a], u is Dictionary, u as Dictionary == d);
			var counts:Dictionary = new Dictionary();
			var words:Array = ["x", "y", "x", "z", "x"];
			for each (var word:String in words) {
				counts[word] = counts[word] ? counts[word] + 1 : 1;
			}
			trace(counts["x"], counts.y, counts["z"], counts.w);
			var f:Function = function (n:int):int { return n + 1; };
			d[f] = "a function as a key";
			trace(d[f]);
			d[a.toString] = "a method"; // Taken twice, one key, as `==` has it.
			trace(d[a.toString], d[b.toString]);
		}
	}
}

class Key {
	public var name:String;
	public function Key(p_name:String) {
		name = p_name;
	}
	public function toString():String {
		return name;
	}
}
