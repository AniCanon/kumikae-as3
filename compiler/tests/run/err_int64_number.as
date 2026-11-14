package {
	public class err_int64_number {
		public function err_int64_number() {
			trace("before");
			var w:Int64 = 9007199254740993;
			var n:Number = w;
			trace("after");
		}
	}
}
