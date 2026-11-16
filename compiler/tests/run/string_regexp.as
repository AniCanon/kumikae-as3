package {
	// String's match, search, replace and split with a pattern. Checked against Node from a line-by-line
	// copy, except where a line says it is AS3's own.
	public class string_regexp {
		public function string_regexp() {
			var s:String = "The rain in Spain stays mainly";
			trace(s.match(/ain/g));
			trace(s.match(/(r)ain/));
			trace(s.match(/xyz/g), s.match(/xyz/));
			// A String to match or search for is made a RegExp, as ECMAScript says (AS3's by recollection).
			trace(s.search(/Spain/), s.search(/x/), s.search("a.n"), s.search());
			trace(s.match("ai"), "aXbX".match("X"));
			trace("abc".match(/x*/g).length);

			trace(s.replace(/ain/g, "AIN"));
			trace(s.replace(/(\w+) (\w+)/, "$2 $1"));
			trace("abcde".replace(/c/, "[$&|$`|$'|$$|$1|$0|$]"));
			trace("abc".replace(/(b)/g, "$11-$01-$2"));
			trace("aaa".replace(/a*?/g, "-"));
			trace("x1y22".replace(/\d/g, ""));
			trace("abc".replace(/b/, function(m:String, i:int, all:String):String {
				return "<" + m + i + all + ">";
			}));
			trace("x1y2".replace(/(\d)/g, function(...args):String {
				return "[" + args.join("|") + "]";
			}));
			trace("a-b".replace(/(x)?-/, function(...args):String {
				return String(args[1]);
			}));
			trace("abcb".replace("b", function(m:String, i:int, all:String):String {
				return m.toUpperCase() + i;
			}));
			trace("abc".replace("b", "$&")); // AS3 inserts a String's replacement for a String as it is written.

			var g:RegExp = /a/g;
			g.lastIndex = 2;
			trace("aXa".replace(g, "b"), g.lastIndex);
			g.lastIndex = 2;
			trace("aXa".match(g), g.lastIndex);
			g.lastIndex = 2;
			trace("aXa".search(g), g.lastIndex);

			trace("a,b;c d".split(/[,; ]/));
			trace("a1b22c".split(/(\d+)/));
			trace("abc".split(/(?:)/));
			trace("abc".split(/x*/, 2));
			trace("".split(/x/).length, "".split(/(?:)/).length);
			trace("test".split(/(t)/));
			trace("a1b".split(/(\d)|x/));
			trace("A<B>C".split(/<|>/, 0).length);

			var p:* = /b/;
			var q:* = "b";
			var rep:* = 5;
			trace("abc".replace(p, "X"), "abc".split(p), "abc".match(p), "abc".search(p));
			trace("abc".replace(q, "X"), "abc".replace(/b/, rep), "abc".replace("b", null));
			trace("a.b".replace(".", "!"), "a.b".search("."));
			trace("ab".replace(undefined, "x"), "undefined".replace(undefined, "x"));
		}
	}
}
