package {
	// What a RegExp raises. The messages are this compiler's, but the second, which is AS3's TypeError #1100
	// as recalled (not checked against Flash). Flash reads a pattern with PCRE, and is recalled to make a
	// RegExp that never matches of one it can't read; ECMAScript 3 raises a SyntaxError, which is what is
	// done here: a pattern that can't be read is never silently a different one.
	public class regexp_errors {
		public function regexp_errors() {
			try {
				new RegExp("(a");
			} catch (e:SyntaxError) {
				trace(e);
			}
			try {
				new RegExp("a{2,1}", "g");
			} catch (e:SyntaxError) {
				trace(e);
			}
			try {
				new RegExp(/a/, "g");
			} catch (e:TypeError) {
				trace(e);
			}
			// A search that backtracks without end is stopped, and says so.
			try {
				trace(/(a+)+b/.test("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
			} catch (e:Error) {
				trace(e);
			}
			var none:RegExp = null;
			try {
				none.exec("a");
			} catch (e:TypeError) {
				trace(e);
			}
			try {
				trace(none.source);
			} catch (e:TypeError) {
				trace(e);
			}
		}
	}
}
