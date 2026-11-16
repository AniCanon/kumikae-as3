package {
	// replace() with a function runs the program inside the call. Checked against Node from a line-by-line
	// copy, but for the errors, whose messages are AS3's.
	public class string_regexp_calls {
		private var text:String = "a1b2c3";

		public function string_regexp_calls() {
			var re:RegExp = /\d/g;
			// The function uses the same RegExp again, and changes the variable the text was read from.
			trace(text.replace(re, function(m:String, i:int, all:String):String {
				text = "changed";
				return "<" + re.test(m) + "|" + "x9".replace(re, "#") + ">";
			}), text, re.lastIndex);
			try {
				"abc".replace(/b/, function(m:String, i:int, all:String):String {
					throw new Error("from the function");
				});
			} catch (e:Error) {
				trace(e.message);
			}
			try {
				"abc".replace(/b/, function(m:String):String {
					return m;
				});
			} catch (e:ArgumentError) {
				trace(e.message);
			}
			// A pattern or a replacement that is neither a RegExp nor a function is its text.
			trace("abc".replace(/b/, new Words("[b]")), "abc".replace(new Words("b"), "X"), "abc".match(new Words("c")));
			// As a number an object is its text, as a number; beside `+`, its text.
			trace(Number(new Words("12")) + 1, new Words("x") + 1, 1 + new Words("y"), new Words("2") * 3);
			var calls:int = 0;
			trace("aaa".replace(/a/g, function(...rest):int {
				return ++calls;
			}), calls);
		}
	}
}

class Words {
	private var words:String;

	public function Words(p_words:String) {
		words = p_words;
	}

	public function toString():String {
		return words;
	}
}
