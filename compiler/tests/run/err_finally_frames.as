package {
	public class err_finally_frames {
		public function err_finally_frames() {
			try { middle(); } finally { trace("outer finally"); }
		}
		private function middle():void {
			try { inner(); } finally { trace("middle finally"); }
		}
		private function inner():void {
			throw new Error("from inner");
		}
	}
}
