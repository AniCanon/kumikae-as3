package {
	public class closures {
		public function closures() {
			// A function expression, called through its value.
			var twice:Function = function (x:int):int { return x * 2; };
			trace(twice(21));
			// It captures a variable: the one variable, not its value then.
			var count:int = 0;
			var bump:Function = function ():void { count++; };
			bump();
			bump();
			trace(count);
			count = 10;
			bump();
			trace(count);
			// A parameter, captured.
			var add:Function = adder(5);
			trace(add(1), add(2));
			// Two closures of one function share its variables; each call has its own.
			var counters:Vector.<Function> = new Vector.<Function>();
			for (var i:int = 0; i < 3; i++) {
				counters.push(counterFrom(i * 10));
			}
			trace(counters[0](), counters[0](), counters[2]());
			// Nested: a function written in a function written in this one.
			var outer:int = 1;
			var make:Function = function ():Function {
				var inner:int = 2;
				return function ():int { return outer + inner; };
			};
			var got:Function = make();
			outer = 100;
			trace(got());
			// What it gives is untyped where nothing is said.
			trace(typeof function ():void {}, (function ():String { return "now"; })());
		}

		private function adder(by:int):Function {
			return function (x:int):int { return x + by; };
		}

		private function counterFrom(start:int):Function {
			var n:int = start;
			return function ():int { return n++; };
		}
	}
}
