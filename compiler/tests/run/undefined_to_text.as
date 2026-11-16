package {
	// A default is for an argument left out. undefined given to a String parameter is null, as any store
	// into a String makes it (avmplus's coerce_s), and null reads "null": String.as has
	// `indexOf(s:String="undefined")`. An Array's join takes anything (`join(sep=void 0)`, Array.as), and
	// there undefined is the comma; a Vector's takes a String (`join(separator:String=",")`, VectorImpl.as).
	public class undefined_to_text {
		public function undefined_to_text() {
			var nothing:*;
			var text:* = "abc undefined null";
			trace(text.indexOf(), text.indexOf(undefined), text.indexOf(nothing), text.indexOf(null), text.lastIndexOf(undefined));
			trace("abc".indexOf(nothing), "a null".indexOf(nothing), "undefined".indexOf());
			var list:* = [1, 2, 3];
			trace(list.join(), list.join(undefined), list.join(nothing), list.join(null), [1, 2].join(nothing));
			var numbers:* = new <int>[1, 2, 3];
			trace(numbers.join(), numbers.join(undefined), numbers.join(null), numbers.join("-"));
		}
	}
}
