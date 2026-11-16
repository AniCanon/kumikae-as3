package {
	// A static method read as a value is a use of its class: the class is set up then, as a call of the method
	// sets it up, so that the function finds the static fields given their values wherever it is called from.
	public class static_method_value {
		public function static_method_value() {
			var f:Function = Holder.count;
			trace(f(1, 2, 3));
		}
	}
}
class Holder {
	public static var seen:Function = function ():int { return arguments.length; };
	public static var first:int = seen(1, 2);
	public static function count(p_a:int):String {
		return p_a + "/" + arguments.length + "/" + first;
	}
}
