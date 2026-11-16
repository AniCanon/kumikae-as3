package { public class array_basic {
	public function array_basic() {
		var a:Array = [1, "two", 3.5];
		trace(a.length, a);
		a[5] = "far";
		trace(a.length, a, a[4], a[10]);
		a.length = 2;
		trace(a);
		trace(a.push(true, null), a);
		trace(a.pop(), a.pop(), [].pop(), a);
		var b:Array = new Array(3);
		trace(b.length, b, new Array(1, 2), Array(4).length, new Array().length);
		trace(a.concat([3, [4, 5]], 6), a.concat([3, [4, 5]], 6).length);
		trace([3, 1, 2].sort(), [10, 9, 1].sort(), [10, 9, 1].sort(Array.NUMERIC));
		var sorted:Array = ["b", undefined, "a"].sort();
		trace(sorted.length, sorted[2]);
		trace([1, 2, 2].sort(Array.UNIQUESORT), [30, 10, 20].sort(Array.RETURNINDEXEDARRAY));
		trace([3, 1, 2].sort(function (x:*, y:*):Number { return x - y; }), [3, 1, 2].sort(function (x:*, y:*):Number { return x - y; }, Array.DESCENDING));
		trace([1, 2, 3].map(function (x:*, i:int, all:Array):* { return x * 10; }), [1, 2, 3].map(null).length);
		trace([1, 2, 3].filter(function (x:*, i:int, all:Array):Boolean { return x > 1; }));
		trace([1, 2, 3].indexOf(2), ["a", 1].indexOf("1"), [1, 2, 3].lastIndexOf(3));
		var u:* = a;
		trace(a is Array, typeof a, u is Array, (u as Array) == a, [1, [2, 3]].toString());
		var e:Array = [];
		e[0] = new Thing();
		var t:Thing = e[0];
		trace(t.name, e.length);
		var sum:Number = 0;
		for each (var x:* in [1, 2, 4]) { sum += x; }
		trace(sum);
		trace([1, 2, 3].reverse(), [1, 2, 3, 4].slice(1, 3), [1, 2, 3, 4].splice(1, 2, "x"), [0, 1].join(" and "));
		var nested:Array = [[1, 2], [3]];
		var inner:Array = nested[0];
		trace(inner.length, nested.length);
	}
} }
class Thing { public var name:String = "kept"; }
