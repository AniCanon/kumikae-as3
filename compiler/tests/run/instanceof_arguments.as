package { public class instanceof_arguments {
	public function instanceof_arguments() {
		// Function(x): x if it is a function, null for null, else AS3's TypeError #1034.
		var f:Function = add;
		var held:* = f;
		var same:Function = Function(held);
		var none:Function = Function(null);
		trace(same === f, none, Function(f)(1, 2), Function(undefined));
		try { Function(5); } catch (e:TypeError) { trace(e.message); }
		try { Function(this); } catch (e:TypeError) { trace(e.errorID); }

		// instanceof: the class of the object or one it extends; never an interface, which is in no
		// prototype chain; a number is a Number and never an int or a uint (avmplus, from recollection).
		var sub:Base = new Sub();
		var any:* = sub;
		var nothing:Base = null;
		trace(sub instanceof Sub, sub instanceof Base, sub instanceof Object, sub instanceof Named, any instanceof Named, sub is Named);
		trace(any instanceof Base, any instanceof Sub, any instanceof Array, new Base() instanceof Sub, nothing instanceof Base, null instanceof Object);
		var n:* = 3;
		var s:* = "s";
		trace(n instanceof Number, n instanceof int, n instanceof uint, 3 instanceof int, 2.5 instanceof Number, s instanceof String, "s" instanceof String);
		trace(true instanceof Boolean, n instanceof String, [] instanceof Array, [] instanceof Object, new <int>[] instanceof Vector.<int>,
				new <int>[] instanceof Vector.<Number>, f instanceof Function, {} instanceof Object, new Error() instanceof Error);

		// arguments: an Array of what the caller passed, made as the function begins, with the function
		// itself as `callee`, which a loop over it passes by.
		trace(counted(), counted(1), counted(1, "two"), counted(1, "two", null));
		trace(listed("a", 2), changed(1, 2), typeof arguments, arguments.length);
		var inner:Function = function (x:int, y:int = 9):String { return arguments.length + ":" + arguments.join(); };
		trace(inner(1), inner(1, 2), again(3), keys(1, 2));
		trace(factorial(5), holder(7)[0], holder(7).length);

		// new Object(x), as Object(x): the value, or a new Object for null and undefined.
		var text:String = "text";
		trace(new Object(text), new Object(5) + 1, new Object(null) != null, new Object(undefined) != null, new Object() != null);
		var fresh:Object = new Object(null);
		fresh.a = 1;
		trace(fresh.a);
	}
	private function add(x:int, y:int):int { return x + y; }
	private function counted(a:* = 0, b:* = "b", c:* = 5):int { return arguments.length; }
	private function listed(a:String, b:int):String { arguments.push("more"); return arguments.join("|") + " " + (arguments is Array) + " " + (arguments === arguments); }
	// What was passed, not what a parameter holds later.
	private function changed(a:int, b:int):String { a = 10; return arguments[0] + "," + a; }
	private function again(n:int):String { return n > 0 ? n + arguments.callee(n - 1) : "."; }
	private function keys(a:int, b:int):String { var all:Array = []; for (var k:String in arguments) { all.push(k); } return all.join(); }
	private static function factorial(n:int):int { return n < 2 ? 1 : n * arguments.callee(n - 1); }
	private function holder(x:int):Array { return arguments; }
} }
interface Named {}
class Base implements Named {}
class Sub extends Base {}
