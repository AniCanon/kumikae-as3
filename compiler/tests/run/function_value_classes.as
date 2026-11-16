package { public class function_value_classes {
	// A call through a function value checks each object argument against the class its parameter holds.
	public function function_value_classes() {
		var f:Function = takes;
		trace(f(new Thing()));
		try { f(new Other()); } catch (e:TypeError) { trace("TypeError", e.message.indexOf("Thing") >= 0); }
		var g:Function = takesList;
		trace(g([1, 2]));
		try { g(new <int>[1]); } catch (e:TypeError) { trace("TypeError", e.message.indexOf("Array") >= 0); }
		var h:Function = takesFunction;
		trace(h(takes));
		try { h(new Thing()); } catch (e:TypeError) { trace("TypeError", e.message.indexOf("Function") >= 0); }
	}
	private function takes(t:Thing):String { return t.name; }
	private function takesList(a:Array):int { return a.length; }
	private function takesFunction(f:Function):String { return "called"; }
} }
class Thing { public var name:String = "thing"; }
class Other { public var size:int = 3; }
