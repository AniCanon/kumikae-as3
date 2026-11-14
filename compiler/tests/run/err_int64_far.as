package {
	public class err_int64_far {
		public function err_int64_far() {
			trace("before");
			var n:Number = 1e19;
			var w:Int64 = n;
			trace("after");
		}
	}
}
