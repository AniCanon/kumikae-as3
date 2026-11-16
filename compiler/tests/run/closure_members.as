package { public class closure_members {
	private var count:int = 0;
	public function closure_members() {
		// A function written in a method sees the object's members, as AS3's scope chain has it.
		var f:Function = function (by:int):int { count += by; bump(); return doubled; };
		trace(f(1), count);
		var nested:Function = function ():Function { return function ():int { bump(); return count; }; };
		trace(nested()(), count);
		var kept:Function = later();
		trace(kept(), kept(), count);
		var asValue:Function = function ():Function { return bump; };
		asValue()();
		trace(count);
		var ticks:Vector.<int> = new <int>[1, 2, 3];
		ticks.forEach(function (x:int, i:int, all:Vector.<int>):void { count += x; });
		trace(count);
	}
	private function bump():void { count += 10; }
	public function get doubled():int { return count * 2; }
	private function later():Function { var step:int = 100; return function ():int { count += step; return count; }; }
} }
