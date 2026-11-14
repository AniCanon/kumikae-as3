package {
	public class err_int64_result {
		public function err_int64_result() {
			trace("before");
			var i:int = wide();
			trace("after");
		}
		private function wide():Int64 { return 3000000000; }
	}
}
