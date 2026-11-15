package {
	public class object_ir {
		public function object_ir() {
			var o:Object = { a: 1, 2: "b" };
			o.c = o["a"];
			for (var k:String in o) {
				trace(k, delete o[k], k in o);
			}
			o.f = function ():int { return 3; };
			trace(o.f(), o.hasOwnProperty("f"));
		}
	}
}
