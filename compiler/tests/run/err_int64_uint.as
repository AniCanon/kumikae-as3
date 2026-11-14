package {
	public class err_int64_uint {
		public function err_int64_uint() {
			trace("before");
			var w:Int64 = -1;
			var u:uint = w;
			trace("after");
		}
	}
}
