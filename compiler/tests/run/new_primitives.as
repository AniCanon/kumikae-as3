package { public class new_primitives {
	// `new String(x)` and the like: AS3 has no wrapper objects, so each is the conversion written as a call
	// (avmplus constructs the primitive itself). With nothing to convert, the type's empty value.
	public function new_primitives() {
		var s:String = new String("x");
		var n:Number = new Number("1.5");
		var b:Boolean = new Boolean("yes");
		var i:int = new int(3.9);
		var u:uint = new uint(-1);
		trace(s, n, b, i, u);
		trace(new String(12) + "!", new String(null), new String(undefined), new String([1, 2]), new Number(true) + 1);
		trace(new Boolean(0), new Boolean(""), new int("12abc"), new int("0x10"), new uint(4294967296 + 5));
		trace(new String() == "", new Number(), new Boolean(), new int(), new uint());
		trace(String() == "", Number(), Boolean(), int(), uint());
		trace(typeof new String("a"), typeof new Number(1), typeof new Boolean(true), new String("a") === "a", new Number(2) === 2);
		var held:* = 2.5;
		trace(new int(held), new String(held), new Boolean(held), new Number(held) + 1);
		var o:Object = new String("in an Object");
		trace(o, o is String);
	}
} }
