package {
	public class err_vector_text {
		public function err_vector_text() {
			trace("before");
			var v:Vector.<Loud> = new Vector.<Loud>();
			v.push(new Loud());
			var s:String = v.join(",");
			trace("after", s);
		}
	}
}

class Loud {
	public function toString():String {
		throw new Error("boom");
	}
}
