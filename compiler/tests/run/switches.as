package {
	public class switches {
		public function switches() {
			for (var i:int = 0; i < 6; i++) {
				trace(i, small(i), middle(i), none(i));
			}
			trace(word("a"), word("bb"), word(""), word(null));
			trace(mixed(1), mixed(2), mixed(3));
			trace(loop(), nested(), effects());
			trace(flag(true), flag(false), wide(5));
		}

		// Falling through, and a default at the end.
		private function small(n:int):String {
			var out:String = "";
			switch (n) {
				case 0:
					out += "zero";
				case 1:
					out += "one";
					break;
				case 2: {
					out += "two";
					break;
				}
				case 3:
				case 4:
					out += "three-or-four";
					break;
				default:
					out += "many";
			}
			return out;
		}

		// A default in the middle is where a value with no case starts, and it falls through.
		private function middle(n:int):String {
			var out:String = "";
			switch (n) {
				case 0:
					out += "a";
					break;
				default:
					out += "d";
				case 1:
					out += "b";
					break;
				case 2:
					out += "c";
			}
			return out;
		}

		private function none(n:int):int {
			var out:int = -1;
			switch (n) {
				case 4:
					out = 40;
					break;
				case 2 + 3:
					out = 50;
			}
			return out;
		}

		private function word(s:String):int {
			switch (s) {
				case "a":
					return 1;
				case "b" + "b":
					return 2;
				case null:
					return 3;
				case "":
					return 4;
			}
			return 0;
		}

		// A case is compared as === compares: 2 is 2.0, and 1.5 is not 1.
		private function mixed(n:int):String {
			switch (n) {
				case 1.5:
					return "never";
				case 2.0:
					return "two";
				case "3":
					return "text";
			}
			return "no";
		}

		// continue inside a switch continues the loop around it; break leaves the switch only.
		private function loop():String {
			var out:String = "";
			for (var i:int = 0; i < 5; i++) {
				switch (i) {
					case 1:
						continue;
					case 3:
						break;
					default:
						out += "d";
				}
				out += i;
			}
			return out;
		}

		// break inside a loop inside a case leaves that loop.
		private function nested():String {
			var out:String = "";
			switch (1) {
				case 1:
					for (var i:int = 0; i < 9; i++) {
						if (i == 2) {
							break;
						}
						out += i;
					}
					out += "after";
					break;
				case 2:
					out += "no";
			}
			return out;
		}

		private var calls:String = "";
		private function note(n:int):int {
			calls += n;
			return n;
		}
		// The subject is read once; cases are tried in order until one matches, and no further.
		private function effects():String {
			switch (note(2)) {
				case note(1):
					calls += "x";
					break;
				case note(2):
					calls += "y";
					break;
				case note(3):
					calls += "z";
			}
			return calls;
		}

		private function flag(b:Boolean):String {
			switch (b) {
				case true:
					return "yes";
				default:
					return "no";
			}
		}

		private function wide(n:Number):String {
			switch (n) {
				case 5:
					return "five";
			}
			return "other";
		}
	}
}
