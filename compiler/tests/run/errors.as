package {
	public class errors {
		private var log:String = "";

		public function errors() {
			// A thrown Error is caught by its class, or by a base class, or by an untyped catch.
			try {
				throw new RangeError("too far");
			} catch (e:TypeError) {
				trace("not this one");
			} catch (e:Error) {
				trace("caught", e.message, e);
			}
			try {
				var nothing:Item;
				nothing.name = "x";
			} catch (e:TypeError) {
				trace("null:", e.message);
			}
			// Any value can be thrown.
			try {
				throw "plain text";
			} catch (e:String) {
				trace("a string:", e);
			}
			try {
				throw 42;
			} catch (e:*) {
				trace("anything:", e, typeof e);
			}
			try {
				throw new Item("thrown");
			} catch (e:Item) {
				trace("an object:", e.name);
			}
			// An error passes through functions until something catches it.
			try {
				deep(3);
			} catch (e:Error) {
				trace("from deep:", e.message);
			}
			// finally runs on every way out.
			trace(finallyNormal(), finallyCaught(), finallyReturn(), log);
			log = "";
			try {
				finallyRethrow();
			} catch (e:Error) {
				trace("rethrown:", e.message, log);
			}
			log = "";
			trace(loopBreak(), loopContinue(), log);
			// A catch that does not match lets the error go on.
			try {
				try {
					throw new TypeError("inner");
				} catch (e:RangeError) {
					trace("not this");
				} finally {
					log += "[inner finally]";
				}
			} catch (e:TypeError) {
				trace("outer caught:", e.message, log);
			}
			// Thrown from a catch.
			try {
				try {
					throw new Error("first");
				} catch (e:Error) {
					throw new Error("second from " + e.message);
				}
			} catch (e:Error) {
				trace(e.message);
			}
			// Labels.
			var found:String = "";
			outer: for (var i:int = 0; i < 4; i++) {
				for (var j:int = 0; j < 4; j++) {
					if (j == 2) {
						continue outer;
					}
					if (i == 3) {
						break outer;
					}
					found += i + "" + j + " ";
				}
			}
			block: {
				found += "in ";
				if (found.length > 0) {
					break block;
				}
				found += "never";
			}
			trace(found);
			// &&= and ||=.
			var a:Boolean = true;
			var b:Boolean = false;
			a &&= false;
			b ||= true;
			var s:String = null;
			s ||= "default";
			var t:String = "set";
			t &&= t + "!";
			trace(a, b, s, t);
		}

		private function deep(n:int):int {
			if (n == 0) {
				throw new Error("bottom");
			}
			return deep(n - 1) + 1;
		}

		private function finallyNormal():String {
			try {
				log += "a";
			} finally {
				log += "b";
			}
			return "normal";
		}

		private function finallyCaught():String {
			try {
				throw new Error("x");
			} catch (e:Error) {
				log += "c";
			} finally {
				log += "d";
			}
			return "caught";
		}

		private function finallyReturn():String {
			try {
				return "returned";
			} finally {
				log += "e";
			}
		}

		private function finallyRethrow():void {
			try {
				throw new Error("goes on");
			} finally {
				log += "f";
			}
		}

		private function loopBreak():int {
			var n:int = 0;
			for (var i:int = 0; i < 10; i++) {
				try {
					if (i == 2) {
						break;
					}
					n++;
				} finally {
					log += "g" + i;
				}
			}
			return n;
		}

		private function loopContinue():int {
			var n:int = 0;
			for (var i:int = 0; i < 3; i++) {
				try {
					if (i == 1) {
						continue;
					}
					n += 10;
				} finally {
					log += "h" + i;
				}
			}
			return n;
		}
	}
}

class Item {
	public var name:String;
	public function Item(name:String) {
		this.name = name;
	}
}
