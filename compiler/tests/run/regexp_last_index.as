package {
	// lastIndex is an int, as avmplus declares it (core/RegExp.as: `get lastIndex():int`, `set lastIndex(i:int)`):
	// what is written is made an int as AS3 makes one (ECMAScript's ToInt32), typed or by name. A negative
	// one finds nothing, and a global search that finds nothing sets it to 0 (core/RegExpObject.cpp).
	public class regexp_last_index {
		public function regexp_last_index() {
			var r:RegExp = /a/g;
			var n:Number = 1.5;
			r.lastIndex = n;
			trace(r.lastIndex);
			r.lastIndex = -1.5;
			trace(r.lastIndex);
			r.lastIndex = NaN;
			trace(r.lastIndex);
			r.lastIndex = 4294967297;
			trace(r.lastIndex);
			r.lastIndex = -1;
			trace(r.exec("aaa"), r.lastIndex);
			var u:* = /a/g;
			u.lastIndex = 1.5;
			trace(u.lastIndex);
			u.lastIndex = -1.5;
			trace(u.lastIndex);
			u.lastIndex = NaN;
			trace(u.lastIndex);
			u.lastIndex = 4294967297;
			trace(u.lastIndex);
			u.lastIndex = "3";
			trace(u.lastIndex, u.lastIndex is int, typeof u.lastIndex);
			u.lastIndex = 2.9;
			trace(u.exec("aaaa").index, u.lastIndex);
			var i:int = r.lastIndex;
			trace(i);
		}
	}
}
