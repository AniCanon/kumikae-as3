package {
	// JSON.parse, by the grammar and the steps of ECMA-262 5th edition (15.12.1, 15.12.2), with what the
	// interpreter's untyped values hold: a whole number is an int. Every line was worked out by hand from
	// the specification, then run through Node as a line-by-line JavaScript copy, which gives the same but
	// for two: the order of the key "3", which Node moves to the front (it orders integer-like keys first)
	// and this interpreter keeps where it was written; and the last line, where Node gives undefined.
	public class json_parse {
		public function json_parse() {
			var o:Object = JSON.parse(' {"b": 1, "a": [true, false, null], "c": {"d": "x"}, "3": -0.5e1} ');
			trace(JSON.stringify(o));
			var keys:Array = [];
			for (var k:String in o) {
				keys.push(k);
			}
			trace(keys);
			trace(o.b is int, o["3"] is int, o["3"], o.a is Array, o.a.length, typeof o.c, o.c.d);

			var n:Array = JSON.parse("[0, -0, 1.5, 2.0, 1e3, 1E-2, 12345678901, 1e400, -1e400, 0.1, 5e-324]") as Array;
			trace(n);
			trace(1 / n[1], n[2] is int, n[3] is int, n[4] is int, n[9] is int);

			var s:Array = JSON.parse('["a\\"b", "\\\\", "\\/", "\\b\\f\\n\\r\\t", "\\u0041\\u00e9", "\\ud83d\\ude00", "\\ud83d", "é", ""]') as Array;
			trace(s[0], s[1], s[2], s[4], s[7], s.length);
			trace(codes(s[3]), codes(s[5]), codes(s[6]), s[5] == "😀", String(s[8]).length);

			trace(JSON.parse("\t\r\n 7 \n"), JSON.parse("true"), JSON.parse("false"), JSON.parse("null"), JSON.parse('"text"'));
			trace(JSON.stringify(JSON.parse('{"a":1,"b":2,"a":3}')));
			trace(JSON.stringify(JSON.parse("{}")), JSON.stringify(JSON.parse("[]")), JSON.stringify(JSON.parse(" [ [ ] , { } ] ")));

			// The reviver is called on each value after its own parts, the root last with the key "".
			var log:Array = [];
			var revived:Object = JSON.parse('{"a":[1,2],"b":{"c":3}}', function(k:String, v:*):* {
				log.push(k);
				return v is Number ? v * 10 : v;
			});
			trace(log.join(","));
			trace(JSON.stringify(revived));

			// What it gives undefined for is deleted: an element of an Array leaves a hole.
			var trimmed:Object = JSON.parse('{"x":1,"y":[1,2,3]}', function(k:String, v:*):* {
				return k == "x" || k == "1" ? undefined : v;
			});
			trace(JSON.stringify(trimmed), "x" in trimmed, trimmed.y.length);

			// Keys are Strings, Array indices too.
			var kinds:Array = [];
			JSON.parse("[[5]]", function(k:*, v:*):* {
				kinds.push(typeof k + ":" + k);
				return v;
			});
			trace(kinds.join(", "));

			// The root revived to undefined: parse() is declared to give an Object, so it is null.
			trace(JSON.parse("1", function(k:String, v:*):* {
				return undefined;
			}));
		}

		private function codes(p_text:String):String {
			var out:Array = [];
			for (var i:int = 0; i < p_text.length; i++) {
				out.push(p_text.charCodeAt(i));
			}
			return out.join(" ");
		}
	}
}
