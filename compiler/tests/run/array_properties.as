// An Array's properties other than its indices: any key that is not a whole number from 0 to 2^32 - 2,
// written as a number or as text. They are not elements: length does not count them.
package {
	public class array_properties {
		public function array_properties() {
			var a:Array = [10, 20];
			a.label = "scores";
			a["kind"] = "ints";
			a[-1] = "minus one";
			a[1.5] = "half";
			a["2"] = 30; // "2" is an index.
			a["02"] = "not an index";
			trace(a.length, a, a.label, a["label"], a[-1], a["-1"], a[1.5], a[2], a["02"], a.nothing);
			var keys:String = "";
			for (var k:String in a) {
				keys += k + " ";
			}
			trace(keys);
			var values:String = "";
			for each (var v:* in a) {
				values += v + " ";
			}
			trace(values);
			trace("label" in a, a.hasOwnProperty("kind"), "push" in a, a.hasOwnProperty("push"), "length" in a, 1 in a, 5 in a);
			trace(delete a.label, "label" in a, delete a[0], a[0], a.length, delete a.length);
			var i:int = -1;
			var n:Number = 0.25;
			a[i] = "again";
			a[n] = "quarter";
			trace(a[i], a[n], a["0.25"]);
			var u:* = a;
			trace(u.length, u[1], u.kind, u["02"]);
			u.length = 1;
			trace(a.length, a.kind);
			a.length = 0;
			a.extra = function ():String { return "called"; };
			trace(a.extra(), u.extra());
		}
	}
}
