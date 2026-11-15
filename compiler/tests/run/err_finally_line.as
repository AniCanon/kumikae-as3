package {
	public class err_finally_line {
		public function err_finally_line() {
			try {
				throw new Error("from line 5");
			} finally {
				trace("finally ran");
			}
		}
	}
}
