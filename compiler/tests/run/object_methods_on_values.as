package { public class object_methods_on_values {
	// Object's methods on a number, a Boolean and a String, typed and held untyped: every value is an
	// Object in AS3. toLocaleString() is the value's text (Object's calls toString()); a String has its
	// `length` and nothing else of its own, a number and a Boolean nothing.
	public function object_methods_on_values() {
		var i:int = -3;
		var u:uint = 4;
		var n:Number = 1.5;
		var b:Boolean = true;
		var s:String = "abc";
		trace(i.toLocaleString(), u.toLocaleString(), n.toLocaleString(), b.toLocaleString(), s.toLocaleString());
		trace((7).toLocaleString(), "x".toLocaleString(), (1 / 3).toLocaleString(), NaN.toLocaleString());
		trace(i.hasOwnProperty("a"), u.hasOwnProperty("length"), n.hasOwnProperty("toFixed"), b.hasOwnProperty("x"));
		trace(s.hasOwnProperty("length"), s.hasOwnProperty("charAt"), s.hasOwnProperty("0"), s.hasOwnProperty("x"));
		trace(i.propertyIsEnumerable("a"), s.propertyIsEnumerable("length"), b.propertyIsEnumerable("x"));
		trace(i.isPrototypeOf(i), s.isPrototypeOf(s), b.isPrototypeOf({}));
		var held:* = i;
		var heldText:* = s;
		var heldFlag:* = b;
		trace(held.toLocaleString(), heldText.toLocaleString(), heldFlag.toLocaleString());
		trace(held.hasOwnProperty("a"), heldText.hasOwnProperty("length"), heldText.hasOwnProperty("x"), heldFlag.hasOwnProperty("x"));
		trace(held.propertyIsEnumerable("a"), heldText.propertyIsEnumerable("length"), held.isPrototypeOf(held));
		// A null String is AS3's error, after the arguments.
		var none:String = null;
		try { none.toLocaleString(); } catch (e:TypeError) { trace("caught", e.errorID); }
		try { none.hasOwnProperty(said("argument")); } catch (e:TypeError) { trace("caught", e.errorID); }
	}
	private function said(text:String):String { trace(text); return text; }
} }
