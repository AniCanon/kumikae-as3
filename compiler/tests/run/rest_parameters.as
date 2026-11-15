package { public class rest_parameters {
	public function rest_parameters() {
		trace(count(), count(1), count(1, "two", null));
		trace(joined("-"), joined("-", 1, 2, 3), joined("+", [4, 5]));
		trace(withDefault(), withDefault(5), withDefault(5, 6, 7));
		var f:Function = count;
		trace(f(), f(1, 2, 3, 4));
		var g:Function = function (first:int, ...others):String { return first + "/" + others.length; };
		trace(g(9), g(9, 8, 7));
		var made:Made = new Made("a", "b", "c");
		trace(made.parts);
		var base:Base = new Base();
		var derived:Base = new Sub();
		trace(base.virtualRest(1, 2), derived.virtualRest(1, 2));
		trace(typeof restOf(1), restOf(1) is Array, restOf(1, true)[0]);
	}
	private function count(...values):int { return values.length; }
	private static function joined(by:String, ...values):String { return values.join(by); }
	private function withDefault(start:int = 1, ...more):String { return start + ":" + more; }
	private function restOf(first:*, ...rest):Array { return rest; }
} }
class Made { public var parts:String; public function Made(...p) { parts = p.join(""); } }
class Base { public function virtualRest(...all):int { return all.length; } }
class Sub extends Base { override public function virtualRest(...all):int { return all.length * 10; } }
