package {
	public class finally_cancelled {
		public function finally_cancelled() {
			trace(byContinue(), byBreak(), byCaught());
		}
		// A return cancelled inside a finally leaves the first return's value standing.
		private function byContinue():int {
			try { return 1; } finally { for (var i:int = 0; i < 1; i++) { try { return 2; } finally { continue; } } }
			return 9;
		}
		private function byBreak():int {
			try { return 1; } finally { L: try { return 2; } finally { break L; } }
			return 9;
		}
		private function byCaught():int {
			try { return 1; } finally { try { try { return 2; } finally { throw new Error("x"); } } catch (e:Error) {} }
			return 9;
		}
	}
}
