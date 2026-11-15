package {
	import flash.utils.Dictionary;

	// JSON.stringify, by the steps of ECMA-262 5th edition (15.12.3). Every line was worked out by hand from
	// the specification; Node agrees with each line that has no Date, Dictionary, Vector or class of the
	// program in it, but for the key "3", which Node moves to the front (it orders integer-like keys first).
	// Rulings of this interpreter: a class's object is written by its public fields, the base class's
	// first, then its public getters, in the order declared (Flash's order is its traits', not known); a key
	// of a replacer Array that such an object lacks is passed over, as ECMAScript's Get gives undefined; a
	// Date is its toJSON(), its toString() as Flash Player 11 has it; a Dictionary's toJSON() is the text
	// "Dictionary" (avmplus's, from recollection); a value written as nothing at all gives a null String,
	// what stringify()'s declared String makes of undefined, and undefined given as the value is null.
	public class json_stringify {
		public function json_stringify() {
			trace(JSON.stringify(null), JSON.stringify(true), JSON.stringify(false), JSON.stringify(3), JSON.stringify(-7));
			trace(JSON.stringify(0.1), JSON.stringify(1e21), JSON.stringify(1.5e-7), JSON.stringify(-0), JSON.stringify(1.2345678901234568e20));
			trace(JSON.stringify(NaN), JSON.stringify(Infinity), JSON.stringify(-Infinity), JSON.stringify(2 / 3));
			trace(JSON.stringify("a\"b\\c/é" + String.fromCharCode(8, 9, 10, 12, 13, 1, 31) + "😀"));

			var o:Object = {b: 1, a: [1, "x", null, true], c: {d: undefined, e: function():void {}}, 3: "three"};
			trace(JSON.stringify(o));
			trace(JSON.stringify([undefined, function():void {}, NaN, {}, []]));
			var holes:Array = [];
			holes[2] = 1;
			holes["extra"] = 5;
			trace(JSON.stringify(holes));
			trace(JSON.stringify(function():void {}) === null, JSON.stringify(undefined));

			var ints:Vector.<int> = new <int>[1, -2, 3];
			var numbers:Vector.<Number> = new <Number>[0.5, NaN];
			var texts:Vector.<String> = new <String>["a", null];
			var points:Vector.<Point> = new <Point>[new Point(1, 2), null];
			trace(JSON.stringify(ints), JSON.stringify(numbers), JSON.stringify(texts), JSON.stringify(points));
			trace(JSON.stringify(new Point3(1, 2, 3)));
			trace(JSON.stringify({p: new Tagged(), q: [new Tagged()]}), JSON.stringify(new Tagged()));
			trace(JSON.stringify({toJSON: function(k:String):* { return "own " + k; }}));
			trace(JSON.stringify(new Date(0)) == '"' + new Date(0).toString() + '"', JSON.stringify([new Date(0)]));
			var dictionary:Dictionary = new Dictionary();
			dictionary["a"] = 1;
			trace(JSON.stringify(dictionary), dictionary.toJSON("k"), JSON.stringify({d: dictionary}));
			var same:Object = {};
			trace(JSON.stringify([same, same, {s: same}]));

			// A replacer function is given each key and value, the root first with the key "".
			var seen:Array = [];
			trace(JSON.stringify({a: 1, b: "x", c: [1, 2]}, function(k:String, v:*):* {
				seen.push(k);
				return v is Number ? v * 2 : v;
			}));
			trace(seen.join(","));
			trace(JSON.stringify({a: 1, b: 2}, function(k:String, v:*):* {
				return k == "a" ? undefined : v;
			}));
			trace(JSON.stringify(1, function(k:String, v:*):* {
				return undefined;
			}));
			trace(JSON.stringify({a: 1}, function(k:String, v:*):* {
				return k == "" ? [v, new Point(5, 6)] : v;
			}));

			// A replacer Array names the keys of objects, in its order, once each; Arrays keep every element.
			trace(JSON.stringify({b: 1, a: 2, c: {a: 3, z: 4}, 5: 5}, ["a", "c", "a", 5, true, null]));
			trace(JSON.stringify([{a: 1, b: 2}, new Point(7, 8)], ["b", "y"]));

			// space: a number of spaces up to 10, or the first 10 units of a String.
			var nested:Object = {a: [1, {}], b: [], c: {d: "e"}};
			trace(JSON.stringify(nested, null, 2));
			trace(JSON.stringify([1, [2]], null, "--"));
			trace(JSON.stringify([1], null, 20));
			trace(JSON.stringify([1], null, "abcdefghijkl"));
			trace(JSON.stringify([1], null, 2.7), JSON.stringify([1], null, 0), JSON.stringify([1], null, -3), JSON.stringify([1], null, ""));
			trace(JSON.stringify([1], null, true), JSON.stringify([1], null, new Point(0, 0)));
			trace(JSON.stringify({a: 1}, ["a"], "\t"));
		}
	}
}

class Point {
	public var x:Number;
	public var y:Number;
	private var secret:int = 1;
	public static var count:int = 0;
	public var none:Object = null;
	public var nothing:*;

	public function Point(p_x:Number, p_y:Number) {
		x = p_x;
		y = p_y;
	}

	public function get sum():Number {
		return x + y;
	}

	public function set only(p_value:int):void {}

	public function move():void {}
}

class Point3 extends Point {
	public var z:Number;

	public function Point3(p_x:Number, p_y:Number, p_z:Number) {
		super(p_x, p_y);
		z = p_z;
	}

	public function get size():int {
		return 3;
	}
}

class Tagged {
	public var hidden:int = 1;

	public function toJSON(p_key:String):* {
		return "tag " + p_key;
	}
}
