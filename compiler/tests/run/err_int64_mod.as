package {
	public class err_int64_mod {
		public function err_int64_mod() {
			trace("before");
			var w:Int64 = 7;
			var zero:Int64 = 0;
			w = w % zero;
			trace("after");
		}
	}
}
