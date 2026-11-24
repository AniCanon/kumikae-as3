package {
	public class string_join {
		public function string_join() {
			var a:String = "start/";
			var b:String = "middle😀";
			var c:String = "/end";
			var total:int = 0;
			for (var i:int = 0; i < 1000000; ++i) {
				var s:String = a + b + c;
				total += s.length;
			}
			trace(total);
		}
	}
}
