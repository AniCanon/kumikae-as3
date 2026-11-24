package {
	public class string_append {
		public function string_append() {
			var piece:String = "abcdé😀";
			var total:int = 0;
			for (var j:int = 0; j < 80; ++j) {
				var s:String = "";
				for (var i:int = 0; i < 2000; ++i) s += piece;
				total += s.length;
			}
			trace(total);
		}
	}
}
