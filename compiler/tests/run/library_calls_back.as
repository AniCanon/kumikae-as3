package { public class library_calls_back {
	public function library_calls_back() {
		// The delimiter's own words are asked for from deep in the program's calls: what split() was given
		// is still what it works on afterwards.
		var pieces:Array = "a-b-c".split(new Delimiter(3000));
		trace(pieces.length, pieces);
		trace(nested(40));
		try {
			decodeURI("%");
		} catch (e:URIError) {
			trace(e.errorID, e.message);
		}
		trace(encodeURIComponent("a b&c"), decodeURIComponent("a%20b"));
		trace("x".split(new Delimiter(-1)));
	}
	// A split whose delimiter splits, to a depth.
	private function nested(p_depth:int):String {
		return "a,b".split(new Nested(p_depth)).join("+");
	}
} }
class Delimiter {
	private var depth:int;
	public function Delimiter(p_depth:int) {
		depth = p_depth;
	}
	public function toString():String {
		if (depth < 0) {
			return decodeURI("%E0%A4%A");
		}
		return sink(depth);
	}
	private function sink(p_left:int):String {
		return p_left == 0 ? "-" : sink(p_left - 1);
	}
}
class Nested {
	private var depth:int;
	public function Nested(p_depth:int) {
		depth = p_depth;
	}
	public function toString():String {
		return depth == 0 ? "," : "x,y".split(new Nested(depth - 1)).length == 2 ? "," : "?";
	}
}
