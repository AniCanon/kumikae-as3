package {
	public class err_finally_error {
		public function err_finally_error() {
			try {
				throw new Error("first");
			} finally {
				trace("finally ran");
			}
		}
	}
}
