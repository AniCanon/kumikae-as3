package {
	public class string_ir {
		public function string_ir() {
			var s:String = "abc";
			var i:int = 0;
			trace(s.indexOf("b", i++), (2.5).toFixed(), isNaN(i), String.fromCharCode(i + 64, 66));
		}
	}
}
