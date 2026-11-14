package {
	public class err_int64_nan {
		public function err_int64_nan() {
			trace("before");
			var n:Number = 0 / 0;
			var w:Int64 = n;
			trace("after");
		}
	}
}
