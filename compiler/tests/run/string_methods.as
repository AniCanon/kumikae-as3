package {
	public class string_methods {
		public function string_methods() {
			var s:String = "Hello, world";
			var none:Number = NaN;
			trace(s.charAt(), s.charAt(4), s.charAt(-1), s.charAt(12) == "", s.charAt(1.9), s.charAt(none), s.charAt(Infinity) == "");
			trace(s.charCodeAt(), s.charCodeAt(7), s.charCodeAt(-1), s.charCodeAt(12), s.charCodeAt(4294967296));
			trace(s.indexOf("o"), s.indexOf("o", 5), s.indexOf("o", -3), s.indexOf("z"), s.indexOf(""), s.indexOf("", 40), s.indexOf());
			trace(s.lastIndexOf("o"), s.lastIndexOf("o", 7), s.lastIndexOf("o", -1), s.lastIndexOf("H", 0), s.lastIndexOf(""), s.lastIndexOf("o", none));
			trace(s.substr(7), s.substr(-5), s.substr(-5, 2), s.substr(3, 0) == "", s.substr(3, -1) == "", s.substr(), s.substr(40) == "");
			trace(s.substring(7), s.substring(5, 0), s.substring(-3, 2), s.substring(4, 4) == "", s.substring(none, 2), s.substring(2, 99));
			trace(s.slice(7), s.slice(-5), s.slice(-5, -2), s.slice(5, 2) == "", s.slice(none, 3), s.slice(2, Infinity));
			trace(s.concat(), s.concat("!", 1, true, null, 2.5), "".concat("a", "b"));
			trace(s.toUpperCase(), s.toLowerCase(), "MiXeD 123".toLocaleUpperCase(), "MiXeD 123".toLocaleLowerCase());
			trace(s.replace("o", "0"), s.replace("world", ""), s.replace("x", "y"), s.replace("", "<>"), "a.b.c".replace(".", "-"));
			// AS3 inserts the replacement as it is: "$&" is not the match (ECMAScript would expand it).
			trace("cost".replace("cost", "$& $$"));
			// AS3 compares by UTF-16 units, not by a locale's order (where "B" would follow "a").
			trace("a".localeCompare("b"), "b".localeCompare("a"), "a".localeCompare("a"), "B".localeCompare("a"), "".localeCompare("a"));
			trace(s.toString(), s.valueOf(), s.toString() == s);
			trace(String.fromCharCode(), String.fromCharCode(72, 105), String.fromCharCode(65.9, 65536 + 66, -1 + 65536 * 2) == "AB￿");
			// Case changes one character into one, as in AS3: "ß" stays (ECMAScript today gives "SS").
			var shown:String = String.fromCharCode(0xE9) + String.fromCharCode(0x3B1, 0x416);
			trace(shown, shown.length, shown.toUpperCase(), "ÀÉÎ ΑΒΓ ЖЯ".toLowerCase(), "ÿ ß".toUpperCase(), "ǅ".toLowerCase(), "ǅ".toUpperCase());
			var at:int = 0;
			trace(s.charAt(at++), s.charAt(at++), s.indexOf(s.charAt(at), at), at);
		}
	}
}
