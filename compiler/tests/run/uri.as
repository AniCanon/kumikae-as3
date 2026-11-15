package {
	// escape, unescape and the URI functions. Checked against Node from a line-by-line copy, but for the
	// errors' messages, which are AS3's URIError #1052 as recalled (not checked against Flash). That AS3's
	// escape() leaves out the same characters as ECMAScript's (@*_+-./) is also recollection.
	public class uri {
		public function uri() {
			var text:String = "a b+c/d?e=f&g#h ~!*'();:@$,[]é€\uD83D\uDE00";
			trace(escape(text));
			trace(unescape(escape(text)) == text, unescape("%41%u0042%zz%u12%"), unescape("%E9"));
			trace(encodeURI(text));
			trace(encodeURIComponent(text));
			trace(decodeURI(encodeURI(text)) == text, decodeURIComponent(encodeURIComponent(text)) == text);
			trace(decodeURI("%3B%2F%3F%3A%40%26%3D%2B%24%2C%23%41%C3%A9"), decodeURIComponent("%3B%2F%23%41%C3%A9"));
			trace(escape(), unescape(), encodeURI(), decodeURIComponent());
			var bad:Array = ["%", "%4", "%G1", "%C3", "%C3%28", "%E0%80%80", "%ED%A0%80", "%F4%90%80%80", "%FF"];
			for each (var one:String in bad) {
				try {
					trace(decodeURIComponent(one));
				} catch (e:URIError) {
					trace(one, e);
				}
			}
			try {
				encodeURIComponent(String.fromCharCode(0xD800));
			} catch (e:URIError) {
				trace(e);
			}
			try {
				encodeURI("a" + String.fromCharCode(0xDC00));
			} catch (e:URIError) {
				trace(e);
			}
			try {
				decodeURI("%");
			} catch (e:URIError) {
				trace(e);
			}
			trace(encodeURIComponent(String.fromCharCode(0xD83D, 0xDE00)));
		}
	}
}
