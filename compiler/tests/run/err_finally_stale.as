package {
	public class err_finally_stale {
		public function err_finally_stale() {
			try {
				throw new Error("first");
			} finally {
				for (var i:int = 0; i < 1; i++) {
					try { throw new Error("second"); } finally { continue; }
				}
				try { try { throw new Error("third"); } finally { throw new Error("fourth"); } } catch (e:Error) {}
			}
		}
	}
}
