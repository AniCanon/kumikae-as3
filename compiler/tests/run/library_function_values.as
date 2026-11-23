package {
	// The library's own functions as values: called through a variable, handed to a method that takes a
	// function, and with call and apply.
	public class library_function_values {
		private function twice(f:Function, x:*):* { return f(f(x)); }

		public function library_function_values() {
			var max:Function = Math.max;
			trace(max(1, 5, 3), max(2), max(), Math.min.apply(null, [4, -2, 9]), Math.max.apply(null, []));
			trace(Math.max.apply(null, [1, "7", 3]), Math.max.call(null, 1, NaN), Math.min.length, Math.floor.length);
			var floor:Function = Math.floor;
			trace(floor(2.7), floor("3.9"), twice(Math.sqrt, 16), twice(Math.abs, -4));
			trace([1.5, 2.5, -0.5].map(function(x:Number, i:int, a:Array):Number { return floor(x); }));
			var power:Function = Math.pow;
			trace(power(2, 10), Math.atan2.call(null, 0, 1), Math.round.apply(null, [2.5]));
			var roll:Function = Math.random;
			var rolled:Number = roll();
			trace(rolled >= 0 && rolled < 1);

			var say:Function = trace;
			say("said", 1, null, undefined, [1, 2], true);
			say();
			["a", "b"].forEach(trace);
			trace.apply(null, ["applied", 2]);
			trace.call(null, "called");

			var parse:Function = parseInt;
			trace(parse("12"), parse("ff", 16), parse(), parseInt.call(null, "0x1F"), parseInt.length);
			var real:Function = parseFloat;
			trace(real("1.5e3"), real("x"), isNaN.call(null, NaN), isNaN.apply(null, ["7"]), twice(isFinite, 1));
			var not:Function = isNaN;
			trace(not(), not(1), not("text"));
			trace(String.fromCharCode.apply(null, [72, 105]), String.fromCharCode.call(null, 65), String.fromCharCode.apply(null, []).length);
			var chars:Function = String.fromCharCode;
			trace(chars(97, 98, 99), chars(), chars.length);
			var encode:Function = encodeURIComponent;
			trace(encode("a b&c"), decodeURIComponent.call(null, "a%20b"), escape.apply(null, ["a b"]), unescape.call(null, "a%20b"));
			var json:Function = JSON.stringify;
			trace(json({ a: 1 }), json([1, 2], null, 1).length, JSON.parse.call(null, "[1,2]").length);
			var utc:Function = Date.UTC;
			trace(utc(2000, 0), Date.UTC.apply(null, [2000, 0, 2]), Date.parse.call(null, "Sat Jan 1 00:00:00 GMT+0000 2000"));

			// One function, however it is read.
			trace(Math.max == Math.max, max === Math.max, trace == say, parseInt == parse, Math.max == Math.min, typeof max);
			var table:Object = {};
			table[Math.max] = 1;
			var untyped:* = Math.max;
			trace(untyped(1, 2), untyped == max, untyped is Function);

			// A typed callback takes one whose signature is the callback's.
			var whole:(Number)=>Number = Math.floor;
			var raised:(Number, Number)=>Number = Math.pow;
			trace(whole(7.9), raised(2, 5));

			// The arguments are counted and converted as a call of a function of the program's is.
			for each (var bad:* in [function():void { floor(); }, function():void { floor(1, 2); }, function():void { power(1); },
					function():void { parse("1", 2, 3); }, function():void { Math.max.apply(null, 5); }]) {
				try {
					bad();
				} catch (e:Error) {
					trace(e.message);
				}
			}
		}
	}
}
