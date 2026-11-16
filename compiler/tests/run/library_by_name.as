package { public class library_by_name {
	// The library's methods called by name on an untyped value: each gives what the typed call gives, its
	// arguments converted as the declared parameters convert them, and one left out its default.
	public function library_by_name() {
		strings();
		arrays();
		vectors();
		loops();
		functions();
		mistakes();
	}
	private function strings():void {
		var s:* = "Hello, World";
		var t:String = "Hello, World";
		trace(s.charAt(1), s.charAt(), s.charAt(99) == "", s.charCodeAt(1), isNaN(s.charCodeAt(99)), s.charAt("2"));
		trace(s.indexOf("o"), s.indexOf("o", 5), s.indexOf("z"), s.lastIndexOf("o"), s.lastIndexOf("o", 5), s.indexOf(""));
		trace(s.substr(7), s.substr(7, 3), s.substr(-5, 3), s.substring(7), s.substring(3, 1), s.slice(-5), s.slice(0, -7), s.slice());
		trace(s.concat("!", 1, null, undefined, true), s.concat(), s.toUpperCase(), s.toLowerCase(), s.toLocaleUpperCase(), s.toLocaleLowerCase());
		trace(s.localeCompare("Hello"), s.localeCompare(t), s.localeCompare("Z"));
		trace(s.replace("o", "0"), s.replace(/o/g, "0"), s.replace(/(l+)/, function (all:String, group:String, at:int, whole:String):String { return "<" + group + at + ">"; }));
		trace(s.match(/o/g), s.match("W(or)"), s.match(/z/), s.search(/W/), s.search("z"));
		trace(s.split(", "), s.split(""). length, s.split(/l+/), s.split("o", 2), s.split().length);
		trace(s.charAt(1) == t.charAt(1), s.substr(2, 3) == t.substr(2, 3), s.split("l").join("|") == t.split("l").join("|"));
	}
	private function arrays():void {
		var a:* = [3, 1, 2];
		trace(a.push(4, 5), a, a.pop(), a.shift(), a.unshift(9, 8), a, a.push());
		trace(a.indexOf(1), a.indexOf(1, 3), a.lastIndexOf(2), a.indexOf("1"), a.reverse(), a.slice(1, 3), a.slice(), a.slice(-2));
		trace(a.concat([7, [8]], 9), a.concat().length, a.join("-"), a.join(), a.join(undefined), a.join(null));
		a.insertAt(1, "in");
		trace(a, a.removeAt(1), a.splice(1, 2), a, a.splice(0, 0, "x", "y"), a, a.splice(2).length, a);
		var numbers:* = [10, 9, 1, 100];
		trace(numbers.sort(), numbers.sort(Array.NUMERIC), numbers.sort(Array.NUMERIC | Array.DESCENDING),
				numbers.sort(function (x:*, y:*):Number { return x - y; }));
		var people:* = [{name: "b", age: 2}, {name: "a", age: 3}];
		people.sortOn("name");
		trace(people[0].name, people.sortOn("age", Array.NUMERIC | Array.DESCENDING)[0].age);
	}
	private function vectors():void {
		var typed:Vector.<int> = new <int>[3, 1, 2];
		var v:* = typed;
		trace(v.push(4.9, "5"), v, v.pop(), v.shift(), v.unshift(9), typed);
		trace(v.indexOf(1), v.lastIndexOf(2), v.reverse(), v.slice(1, 3), v.concat(new <int>[7]), v.join("-"));
		v.insertAt(0, 6);
		trace(v, v.removeAt(0), v.splice(1, 1), v, v.sort(Array.NUMERIC), v.sort(function (x:int, y:int):Number { return y - x; }));
		trace(v.slice() is Vector.<int>, v.concat() !== typed, v.reverse() === typed);
		var words:* = new <String>["b", "a"];
		trace(words.push(1, null), words, words.indexOf("1"), words.sort(0).join());
		var things:* = new <Item>[new Item(2), new Item(1)];
		trace(things.push(new Item(3)), things.indexOf(things[1]), things.join());
		try { things.push(new Object()); } catch (e:TypeError) { trace("caught", e.errorID); }
		try { v.concat([1]); } catch (e:TypeError) { trace("caught", e.errorID); }
		v.fixed = true;
		try { v.push(1); } catch (e:RangeError) { trace("caught", e.errorID); }
	}
	private function loops():void {
		var a:* = [1, 2, 3, 4];
		var seen:Array = [];
		a.forEach(function (x:*, i:int, all:Array):void { seen.push(x * i, all === a); });
		trace(seen);
		trace(a.map(function (x:*, i:int, all:Array):* { return x * 10; }), a.filter(function (x:*, i:int, all:Array):Boolean { return x % 2 == 0; }));
		trace(a.some(function (x:*, i:int, all:Array):Boolean { return x > 3; }), a.every(function (x:*, i:int, all:Array):Boolean { return x > 3; }));
		trace(a.map(null).length, a.filter(null).length, a.some(null), a.every(null), a.forEach(null));
		var v:* = new <int>[1, 2, 3];
		var mapped:* = v.map(function (x:int, i:int, all:Vector.<int>):* { return x + 0.9; });
		trace(mapped, mapped is Vector.<int>, v.filter(function (x:int, i:int, all:Vector.<int>):Boolean { return x != 2; }), v.map(null));
		trace(v.every(function (x:int, i:int, all:Vector.<int>):* { return 1; }), v.some(function (x:int, i:int, all:Vector.<int>):* { return true; }));
		// A callback that takes fewer than it is given is AS3's ArgumentError; a method with an object to
		// call it on is TypeError #1510, as in the typed call.
		try { a.forEach(function (x:*):void {}); } catch (e:ArgumentError) { trace("caught", e.errorID); }
		try { a.forEach(heard, this); } catch (e:TypeError) { trace("caught", e.errorID); }
		try { a.forEach(5); } catch (e:TypeError) { trace("caught", e.errorID); }
		// One taken out of a Vector while the loop runs is a RangeError; out of an Array, undefined.
		try { v.forEach(function (x:int, i:int, all:Vector.<int>):void { all.length = 1; }); } catch (e:RangeError) { trace("caught", e.errorID); }
		var shrunk:Array = [];
		a.forEach(function (x:*, i:int, all:Array):void { all.length = 2; shrunk.push(x); });
		trace(shrunk);
	}
	private function heard(x:*, i:int, all:Array):void {}
	private function add(x:int, y:int = 10):int { return x + y; }
	private function functions():void {
		var f:* = add;
		trace(f.call(null, 1, 2), f.call(null, 1), f.apply(null, [5, 6]), f.apply(null, [5]), f.call(this, 2), f.length);
		var g:* = function (...rest):int { return rest.length; };
		trace(g.call(), g.call(null), g.call(null, 1, 2, 3), g.apply(), g.apply(null), g.apply(null, null), g.apply(null, [1, 2]));
		try { f.apply(null, 5); } catch (e:TypeError) { trace("caught", e.errorID); }
		try { f.call(null); } catch (e:ArgumentError) { trace("caught", e.errorID); }
	}
	private function mistakes():void {
		var s:* = "abc";
		var a:* = [1];
		var v:* = new <int>[1];
		try { s.charAt(1, 2); } catch (e:ArgumentError) { trace(e.message); }
		try { a.insertAt(1); } catch (e:ArgumentError) { trace(e.message); }
		try { v.sort(); } catch (e:ArgumentError) { trace(e.message); }
		try { s.push(1); } catch (e:Error) { trace(e.errorID); }
		try { a.charAt(1); } catch (e:Error) { trace(e.errorID); }
		// A property of the same name on an Array comes first.
		a.push = function (x:*):String { return "own " + x; };
		trace(a.push(5), a.length);
	}
} }
class Item { public var n:int; public function Item(n:int) { this.n = n; } public function toString():String { return "Item" + n; } }
