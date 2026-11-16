package {
	// Null and undefined elements put into words. An Array leaves them out (avmplus core/Array.as, _join:
	// `if (x != null) out += x`, as ECMAScript 3 has it); a Vector of any type writes each as String() gives
	// it (core/VectorImpl.as, join: `s += this[y]`, with no test), so "null" and "undefined".
	public class null_elements_text {
		public function null_elements_text() {
			trace(new Vector.<String>(2));
			var objects:Vector.<Object> = new Vector.<Object>();
			objects.push(null);
			objects.push(1);
			trace(objects, objects.join("-"), String(objects), "" + objects);
			var any:Vector.<*> = new Vector.<*>();
			any.push(undefined);
			any.push(null);
			any.push(2);
			trace(any, any.join("/"), any.toString());
			var typed:Vector.<Point> = new Vector.<Point>(2);
			typed[1] = new Point();
			trace(typed);
			var array:Array = [null, undefined, 1];
			trace(array, array.join("-"), String(array));
			var nested:Array = [objects, [null, 3]];
			trace(nested);
		}
	}
}

class Point {
	public function toString():String {
		return "p";
	}
}
