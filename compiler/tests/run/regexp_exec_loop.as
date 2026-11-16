package {
	// exec() and test() called again and again on the same text, as a loop over a global RegExp's matches
	// does: each starts at lastIndex, counted in UTF-16 units (a character past U+FFFF is two), and each
	// match's `input` is the text searched. A loop over a long text takes time in proportion to it.
	public class regexp_exec_loop {
		public function regexp_exec_loop() {
			var s:String = "é1 ☃22 😀333 x4444";
			var r:RegExp = /\d+/g;
			var m:Array;
			var line:String = "";
			while ((m = r.exec(s)) != null) {
				line += m[0] + "@" + m.index + ">" + r.lastIndex + " ";
				if (m.input != s) {
					trace("input differs");
				}
			}
			trace(line, r.lastIndex);
			var count:int = 0;
			while (r.test(s)) {
				count++;
				line = String(r.lastIndex);
			}
			trace(count, line, r.lastIndex);
			// A new text in the middle of a loop is searched from where the last one left off.
			r.lastIndex = 0;
			trace(r.exec(s).index, r.lastIndex, r.exec("ab12")[0], r.lastIndex, r.exec(s).index, r.lastIndex);
			// The same by name, on a RegExp held untyped.
			var u:* = /😀|☃/g;
			var found:String = "";
			while ((m = u.exec(s)) != null) {
				found += m[0] + m.index + "," + u.lastIndex + ";" + (m.input == s) + " ";
			}
			trace(found);
			var tested:int = 0;
			while (u.test(s)) {
				tested++;
			}
			trace(tested, u.lastIndex);
			// A long text with many matches: each exec() costs what its match costs, not the whole text.
			var parts:Array = [];
			for (var i:int = 0; i < 20000; i++) {
				parts.push("é" + i);
			}
			var long:String = parts.join(" ");
			var n:int = 0;
			var last:int = 0;
			var digits:RegExp = /\d+/g;
			while ((m = digits.exec(long)) != null) {
				n++;
				last = m.index;
			}
			var t:int = 0;
			while (digits.test(long)) {
				t++;
			}
			trace(n, t, last, long.length);
		}
	}
}
