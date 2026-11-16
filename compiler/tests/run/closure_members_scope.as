package {
	// A function expression that names a member of the object it was written in without `this` reaches
	// that object, as AS3's scope chain has it: fields, methods, a method as a value, at any depth.
	public class closure_members_scope {
		private var count:int = 0;
		public var label:String = "start";

		public function closure_members_scope() {
			var step:Function = function(by:int):void {
				count += by;
				label = "stepped " + count;
			};
			step(2);
			step(3);
			trace(count, label);
			var nested:Function = function():Function {
				return function():String {
					return twice(count) + " " + label;
				};
			};
			trace(nested()());
			var named:Function = function():Function {
				return twice;
			};
			trace(named()(21), toString());
			var local:int = 1;
			var both:Function = function():int {
				local++;
				return local + count;
			};
			trace(both(), local);
		}

		private function twice(p_value:int):int {
			return p_value * 2;
		}

		public function toString():String {
			return "[" + label + "]";
		}
	}
}
