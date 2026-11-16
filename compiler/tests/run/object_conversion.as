package { public class object_conversion {
	// Object(x): the value itself, whatever it is, or a new empty Object for null, undefined or nothing
	// (avmplus's ObjectClass, from recollection).
	public function object_conversion() {
		var s:String = "text";
		var list:Array = [1, 2];
		var none:Object = null;
		var nothing:*;
		trace(Object(s), Object(3) + 1, Object(true), Object(list) === list, Object(s) is String);
		trace(Object(none) != null, Object(nothing) != null, Object(null) != null, Object(undefined) != null, Object() != null);
		var made:Object = Object(none);
		made.a = 1;
		trace(made.a, none == null, String(Object(null)));
		trace(typeof Object(1), typeof Object(null), typeof Object("s"));
		// The argument is worked out once.
		trace(Object(counted()), Object(counted()), calls);
	}
	private var calls:int = 0;
	private function counted():Object { calls++; return calls > 1 ? null : "once"; }
} }
