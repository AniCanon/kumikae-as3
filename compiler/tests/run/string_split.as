package { public class string_split {
	public function string_split() {
		trace("a,b,,c".split(","), "a,b,,c".split(",").length);
		trace("abc".split("").length, "abc".split("")[1], "abc".split().length, "abc".split(null)[0]);
		trace("a,b,c".split(",", 2), "a,b,c".split(",", 0).length, "".split(",").length, "".split("").length);
		trace("one--two--".split("--"), "one--two--".split("--").length, "x1y1z".split(1));
		var words:Array = "the quick fox".split(" ");
		trace(words.length, words.join("+"), words[2]);
	}
} }
