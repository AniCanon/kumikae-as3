package {
	public class err_deep {
		public function err_deep() {
			trace(down(100));
			trace(down(1000000));
		}
		private function down(n:int):int {
			return n == 0 ? 0 : 1 + down(n - 1);
		}
	}
}
