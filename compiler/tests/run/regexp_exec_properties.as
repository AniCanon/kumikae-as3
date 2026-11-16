package { public class regexp_exec_properties {
	public function regexp_exec_properties() {
		var r:RegExp = /(?P<word>b+)(c)?/;
		var m:Array = r.exec("aabbbd");
		trace(m.index, m.input, m.word, m[1], m[2], m.length);
		var g:RegExp = /o/g;
		var first:Array = g.exec("foo");
		var second:Array = g.exec("foo");
		trace(first.index, second.index, g.lastIndex);
		var matched:Array = "xyz".match(/y/);
		trace(matched.index, matched.input);
	}
} }
