package {
	public class error_members {
		public var n:int;
		public function error_members() {
			var made:Error = new Error("boom", 42);
			trace(made.message, made.name, made.errorID);
			// One the interpreter raises: its name, and the number in its message.
			var none:error_members = null;
			try { trace(none.n); } catch (e:TypeError) { trace(e.name, e.errorID); }
			// The stack trace is taken where the error is made, below its own constructors.
			trace(new RangeError("range").getStackTrace());
			trace(new Mine().getStackTrace());
			trace(new Error().getStackTrace());
		}
	}
}

class Mine extends Error {
	public function Mine() { super("mine", 7); }
}
