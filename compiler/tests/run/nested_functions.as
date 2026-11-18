package {
	// A function declared inside a function: a variable of that function, set as the function starts.
	public class nested_functions {
		private var total:int = 10;

		public function nested_functions() {
			trace(early(3)); // Called above where it is written.
			function early(n:int):int { return n * 2; }

			function factorial(n:int):int { return n <= 1 ? 1 : n * factorial(n - 1); }
			trace(factorial(5));

			var count:int = 0;
			function bump(by:int = 1):void { count += by; total += by; }
			bump();
			bump(4);
			trace(count, total);

			function sum(...values):Number {
				var result:Number = 0;
				for each (var value:Number in values) { result += value; }
				return result;
			}
			trace(sum(), sum(1, 2, 3.5));

			var held:Function = early;
			trace(held(21), held === early, [1, 2, 3].map(function(x:int, i:int, a:Array):int { return early(x); }));
			trace([3, 1, 2].sort(compare));
			function compare(a:int, b:int):int { return a - b; }

			if (count > 100) {
				function unreached():String { return "declared where the code never goes"; }
			}
			trace(unreached());

			function outer(x:int):Function {
				function inner(y:int):int { return x + y + count; }
				return inner;
			}
			trace(outer(1)(2), outer(10)(20));

			function even(n:int):Boolean { return n == 0 ? true : odd(n - 1); }
			function odd(n:int):Boolean { return n == 0 ? false : even(n - 1); }
			trace(even(10), odd(10));

			var named:Function = function power(base:int, n:int):int { return n == 0 ? 1 : base * power(base, n - 1); };
			trace(named(2, 10));

			function untyped(a:*, b:*):* { return a + b; }
			trace(untyped(1, 2), untyped("a", "b"));
			function nothing():void {}
			trace(nothing());
			trace(counter());
			trace(typeof early, early is Function, early.length);

			try {
				held("x", 2);
			} catch (e:ArgumentError) {
				trace(e.errorID);
			}
			try {
				fails();
			} catch (e2:Error) {
				trace(e2.getStackTrace());
			}
		}

		private static function counter():int {
			var n:int = 0;
			function next():int { return ++n; }
			next();
			next();
			return next();
		}

		private function fails():void {
			function thrower():void { throw new Error("from a nested function"); }
			thrower();
		}
	}
}
