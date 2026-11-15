package {
	public class finally_paths {
		private var log:String = "";

		public function finally_paths() {
			// An error in a finally that a return runs: the finally runs once, and the same try's catch
			// does not take its error.
			try {
				fromFinally();
			} catch (e:Error) {
				trace("f:", e.message, log);
			}
			log = "";
			try {
				ownCatch();
			} catch (e:Error) {
				trace("g:", e.message, log);
			}
			log = "";
			try {
				innerDoesNotCatchOuter();
			} catch (e:Error) {
				trace("h:", e.message, log);
			}
			log = "";
			trace("loop:", loopBreakThrows(), log);
			log = "";
			// break inside a finally leaves the loop around the finally.
			trace(breakInFinally(), log);
			log = "";
			trace(breakInFinallyWithSwitch(), log);
			log = "";
			// return in a finally wins over the return before it, and over an error.
			trace(returnWins(), swallowed(), log);
			// Any value: null and undefined too.
			try {
				throw null;
			} catch (e:Error) {
				trace("never an Error");
			} catch (e) {
				trace("null:", e);
			}
			try {
				throw undefined;
			} catch (e:*) {
				trace("undefined:", e);
			}
			try {
				throw 7;
			} catch (e:Object) {
				trace("as Object:", e);
			}
			// A labelled switch, and break label through a finally.
			var picked:String = "";
			pick: switch (2) {
				case 2:
					picked += "two";
					break pick;
				default:
					picked += "default";
			}
			out: for (var i:int = 0; i < 3; i++) {
				try {
					if (i == 1) {
						break out;
					}
					picked += i;
				} finally {
					picked += "f" + i;
				}
			}
			trace(picked);
		}

		private function fromFinally():int {
			try {
				return 1;
			} finally {
				log += "fin ";
				throw new Error("from finally");
			}
		}

		private function ownCatch():int {
			try {
				return 1;
			} catch (e:Error) {
				log += "WRONG ";
			} finally {
				log += "fin ";
				throw new Error("from finally g");
			}
			return 2;
		}

		private function innerDoesNotCatchOuter():int {
			try {
				try {
					return 1;
				} catch (e:Error) {
					log += "WRONG inner caught " + e.message;
					return 9;
				} finally {
					log += "a";
				}
			} finally {
				log += "b";
				throw new Error("from outer finally");
			}
			return 0;
		}

		private function loopBreakThrows():int {
			var n:int = 0;
			for (var i:int = 0; i < 3; i++) {
				try {
					try {
						if (i == 1) {
							break;
						}
						n++;
					} finally {
						log += "x" + i;
						if (i == 1) {
							throw new Error("e");
						}
					}
				} catch (e:Error) {
					log += "c" + i;
				}
			}
			return n;
		}

		private function breakInFinally():String {
			for (var i:int = 0; i < 3; i++) {
				try {
					for (var j:int = 0; j < 3; j++) {
						log += j;
						if (j == 1) {
							return "returned";
						}
					}
				} finally {
					log += "F";
					break;
				}
			}
			return "after loop";
		}

		private function breakInFinallyWithSwitch():String {
			for (var i:int = 0; i < 3; i++) {
				try {
					switch (i) {
						case 0:
							log += "i0";
							return "returned";
					}
					log += "x";
				} finally {
					log += "F";
					break;
				}
			}
			return "after loop";
		}

		private function returnWins():String {
			try {
				return "first";
			} finally {
				return "finally";
			}
		}

		private function swallowed():String {
			try {
				throw new Error("lost");
			} finally {
				log += "s";
				return "swallowed";
			}
		}
	}
}
