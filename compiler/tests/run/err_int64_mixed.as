package {
	public class err_int64_mixed {
		public function err_int64_mixed() {
			trace("before");
			var w:Int64 = 9007199254740993;
			var n:Number = w + 0.5;
			trace("after");
		}
	}
}
