package {
	public class err_int64_argument {
		public function err_int64_argument() {
			trace("before");
			var w:Int64 = 3000000000;
			take(w);
			trace("after");
		}
		private function take(i:int):void {}
	}
}
