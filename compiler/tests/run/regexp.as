package {
	// RegExp's properties, exec() and test(). Checked against Node from a line-by-line copy, except where a
	// line says it is AS3's own.
	public class regexp {
		public function regexp() {
			var r:RegExp = /a(b+)c/gi;
			trace(r.source, r.global, r.ignoreCase, r.multiline, r.lastIndex);
			trace(r.dotall, r.extended); // AS3's own flags. toString() writes them in the order g, i, m, s, x (recollection).
			trace(r, String(r), "is " + r);
			var m:* = r.exec("xxABBCyyabc");
			trace(m, r.lastIndex);
			m = r.exec("xxABBCyyabc");
			trace(m, r.lastIndex);
			m = r.exec("xxABBCyyabc");
			trace(m, r.lastIndex); // A failed search starts again from 0.
			trace(r.test("abc"), r.lastIndex, r.test("abc"), r.lastIndex);

			var plain:RegExp = new RegExp("(\\d+)-(\\d+)?");
			trace(plain.exec("call 555- now"), plain.lastIndex);
			plain.lastIndex = 3;
			trace(plain.test("5-"), plain.lastIndex); // Not global: lastIndex is not read, nor written on a match.
			trace(plain.test("none"), plain.lastIndex, plain.exec("none"), plain.lastIndex); // Nor on a failure, as in avmplus.
			trace("none".match(plain), plain.lastIndex); // match() of one not global is its exec().

			var g:RegExp = /a/g;
			g.lastIndex = 10;
			trace(g.exec("aaa"), g.lastIndex);
			g.lastIndex = 1;
			g.lastIndex++;
			trace(g.exec("aaa"), g.lastIndex);
			g.lastIndex += 0.5; // Read as a whole number.
			trace(g.exec("aaaa"), g.lastIndex);

			var copy:RegExp = new RegExp(r);
			trace(copy, copy == r, copy.lastIndex, copy.global);
			trace(new RegExp("x", undefined), new RegExp(5, "g"), new RegExp(null).source);
			trace(/z/.exec("abc"), /^\w+$/.test("hello"), /\d/.test("abc"));
			trace(/(a)|(b)/.exec("b")); // A group that took no part is undefined (ECMAScript; AS3's by recollection).
			trace(typeof r, r is RegExp, r is Object);

			// A literal makes a new RegExp each time it is reached, as AS3 compiles it to `new RegExp`.
			for (var i:int = 0; i < 2; i++) {
				var each:RegExp = /q/g;
				trace(each.lastIndex);
				each.lastIndex = 5;
			}
			var u:* = r;
			trace(u, typeof u, u == r);
		}
	}
}
