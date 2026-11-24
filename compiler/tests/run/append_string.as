package {
	public class append_string {
		private function grow(s:String):String { s += "!"; return s; }
		private function remember(s:String):Function {
			return function():String { return s; };
		}
		private function fresh():String { return String.fromCharCode(111, 108, 100); }

		public function append_string() {
			var s:String = fresh();
			var copy:String = s;
			s += "!";
			trace(copy, s);
			s = fresh();
			trace(grow(s), s);
			var captured:Function = remember(s);
			s += "?";
			trace(captured(), s);
			s = fresh();
			var a:Array = [s];
			s += ".";
			trace(a[0], s);
			s = fresh();
			s += s;
			trace(s);
			s = String.fromCharCode(0xd83d, 0xde00);
			trace(s.length);
			// A stale cached count changes these lengths. Omitting the carry makes length
			// rescan and return the same value; test_append_string checks measured() directly.
			s += "é";
			trace(s, s.length);
			var piece:String = String.fromCharCode(0xd83d, 0xde00);
			trace(piece.length);
			s = fresh();
			s += piece;
			s += piece;
			trace(s, s.length);

			var power:String = "x";
			s = "";
			for (var i:int = 0; i < 16; i++) {
				s += power;
				power += power;
			}
			trace(s.length, s.charAt(65534));
			s += "y";
			trace(s.length, s.substr(65534));

			s = fresh();
			try { s += "/retry"; trace(s); }
			catch (error:Error) { trace(error.errorID, s); }
		}
	}
}
