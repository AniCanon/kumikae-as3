package {
	public class try_ir {
		public function try_ir() {
			try {
				trace("body");
			} catch (e:String) {
				trace("text", e);
			} finally {
				trace("finally");
			}
		}
	}
}
