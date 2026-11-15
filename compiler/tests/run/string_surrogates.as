package {
	public class string_surrogates {
		public function string_surrogates() {
			// A character past U+FFFF is two UTF-16 units, and each half is a character of its own, as in AS3.
			var face:String = "a😀b";
			var high:String = face.charAt(1);
			var low:String = face.charAt(2);
			trace(face.length, face.charCodeAt(1), face.charCodeAt(2), high.length, low.length);
			trace(high + low == "😀", "\uD83D\uDE00" == "😀", "\uD83D\uDE00".length, String.fromCharCode(0xD83D, 0xDE00) == "😀");
			trace(String.fromCharCode(0xD83D) + String.fromCharCode(0xDE00) == "😀", high.concat(low) == "😀", high + low + high == "😀\uD83D");
			trace(face.indexOf("\uD83D"), face.indexOf("\uDE00"), face.lastIndexOf("\uDE00"), face.indexOf("b"), face.indexOf("😀"), face.indexOf("b", 2));
			trace(face.substring(0, 2) + face.substring(2) == face, face.slice(2).charCodeAt(0), face.substr(1, 2) == "😀", face.substr(2).length);
			trace(face.toUpperCase() == "A😀B", face.charAt(3), "😀" < "\uFFFF", high < "\uFFFF", "x" + high + low + "y" == "x😀y");
			trace(face.replace("😀", "-"), face.replace(low, "") == "a" + high + "b", (high + "!" + low).length, face.lastIndexOf("\uD83D", 1));
			trace(face.indexOf("\uDE00b"), face.indexOf("😀", 2), face.lastIndexOf("a\uD83D"), "\uDE00".charCodeAt(0), "\uD83D" + "x" == "\uD83Dx");
			trace(new <String>[high, low].join("") == "😀", new <String>[high, low].join(low + high) == "😀😀😀".substring(0, 4));
		}
	}
}
