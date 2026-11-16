package { public class method_values {
	// A method of the library read and not called is a function, bound to what it was read from: it can
	// be kept, called, passed and compared like any other.
	public function method_values() {
		var a:* = [1, 2];
		var push:* = a.push;
		trace(push(3, 4), a, typeof push, push is Function, push.length);
		var s:* = "Hello";
		var upper:Function = s.toUpperCase;
		var at:Function = s["char" + "At"];
		trace(upper(), at(1), at(), s["toUpperCase"](), a["push"](5), a["jo" + "in"]("+"));
		var v:* = new <int>[3, 1, 2];
		var sort:* = v.sort;
		var join:* = v.join;
		trace(sort(Array.NUMERIC) === v, join("-"), v.indexOf.call(null, 2), v.slice.apply(null, [1]));
		var n:* = 255;
		var o:* = {a: 1};
		var fixed:Function = n.toFixed;
		var radix:Function = n.toString;
		var has:Function = o.hasOwnProperty;
		var text:Function = o.toString;
		trace(fixed(1), radix(16), radix(), has("a"), has("b"), text(), true.toString == null);
		// Of a RegExp, a Date and a Function; and a typed value's method is one too.
		var r:* = /l+/g;
		var d:* = new Date(0);
		var test:Function = r.test;
		var time:Function = d.getTime;
		var f:* = function (x:int, y:int):int { return x * y; };
		var call:Function = f.call;
		var apply:Function = f.apply;
		trace(test("hello"), time(), call(null, 3, 4), apply(null, [5, 6]));
		var typed:Array = ["b", "a"];
		var typedSort:Function = typed.sort;
		var word:String = "abc";
		var typedAt:Function = word.charAt;
		var numbers:Vector.<Number> = new <Number>[1.5];
		var typedPush:Function = numbers.push;
		trace(typedSort(), typedAt(2), typedPush(2.5, "3"), numbers);
		// Passed where a function is taken, and called with too few or too many arguments.
		trace(["x", "yy"].map(function (w:String, i:int, all:Array):* { return w.length; }), [3, 1, 2].sort(compare(v.indexOf)));
		try { at(1, 2); } catch (e:ArgumentError) { trace(e.message); }
		try { v.insertAt.call(null, 1); } catch (e:ArgumentError) { trace(e.message); }
		// The same method of the same value is the same function; of another value, another.
		var b:* = [1, 2];
		trace(a.push == a.push, a.push == b.push, a.push == a.pop, s.charAt == s.charAt, push == a.push);
		// It keeps its value alive, and a later change to the value is seen.
		var keep:Function = makeList().push;
		trace(keep("z"), keep("y"));
		// In stack traces the call shows where it was made from, not the library's own frame.
		try { v.forEach.call(null, function (x:int, i:int, all:Vector.<int>):void { throw new Error("thrown"); }); }
		catch (e:Error) { trace(e.getStackTrace().split("\n").length); }
	}
	private function makeList():* { return ["first"]; }
	private function compare(indexOf:Function):Function { return function (x:int, y:int):Number { return indexOf(x) - indexOf(y); }; }
} }
