package {
	// Text that ECMA-262 5th edition's JSON grammar (15.12.1) does not take is a SyntaxError. The message is
	// avmplus's #1132, from recollection, not checked against Flash: it says no more than that, for any
	// fault. Node, run on a JavaScript copy of the list, rejects each of these too, and reads the 1,000
	// levels below. A null text is the same error (avmplus's, from recollection; ECMAScript would read the
	// text "null"). Deeper than 1,000 levels is Error #1023, the interpreter's ruling, where Node has no
	// limit: the walk for a reviver and stringify() go down one level at a time.
	public class json_parse_errors {
		public function json_parse_errors() {
			var bad:Array = ["", " ", "01", "-01", "-", "1.", ".5", "+1", "1e", "1e+", "1.5e3.2", "0x10", "NaN", "Infinity",
				"-Infinity", "'a'", '"a', '"\\x"', '"\\u12"', '"\\u12G4"', '"\\U0041"', '"tab\tin"', "[1,]", "[,1]",
				'{"a":1,}', "{a:1}", "{'a':1}", '{"a" 1}', '{"a":}', '{"a",1}', "[1 2]", "tru", "nul", "True", "true false",
				"1 2", "[", "]", "{", "}", "[1]x", "//c\n1", "/*c*/1", "undefined", '{"a":1]', "[1}"];
			bad.push(String.fromCharCode(34, 0, 34), String.fromCharCode(0xA0) + "1", String.fromCharCode(11) + "1",
				String.fromCharCode(12) + "1", "1" + String.fromCharCode(0x2028));
			for (var i:int = 0; i < bad.length; i++) {
				try {
					trace("parsed", i, JSON.parse(bad[i]));
				} catch (e:SyntaxError) {
					trace(i, e.errorID, e.message);
				}
			}
			try {
				JSON.parse(null);
			} catch (e:SyntaxError) {
				trace("null", e);
			}

			var deep:String = "";
			for (var d:int = 0; d < 1000; d++) {
				deep = "[" + deep + "]";
			}
			trace(JSON.stringify(JSON.parse(deep)).length);
			try {
				JSON.parse("[" + deep + "]");
			} catch (e:Error) {
				trace(e);
			}
			try {
				JSON.parse("[" + deep);
			} catch (e:Error) {
				trace(e);
			}
		}
	}
}
