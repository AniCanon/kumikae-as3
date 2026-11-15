package { public class vector_callbacks {
	public function vector_callbacks() {
		var v:Vector.<int> = new <int>[3, 1, 2];
		var log:String = "";
		v.forEach(function (x:int, i:int, all:Vector.<int>):void { log += i + "=" + x + "/" + all.length + " "; }, this);
		trace(log);
		trace(v.map(function (x:int, i:int, all:Vector.<int>):int { return x * 2; }));
		trace(v.map(function (x:int, i:int, all:Vector.<int>):void {}));
		trace(v.filter(odd), v.filter(odd).length);
		trace(v.some(function (x:int, i:int, all:Vector.<int>):Boolean { return x > 2; }), v.every(odd), v.some(odd));
		var empty:Vector.<int> = new Vector.<int>();
		trace(empty.some(odd), empty.every(odd), empty.map(odd).length);
		empty.forEach(null);
		// No function: no call, and no error (avmplus).
		v.forEach(null);
		trace(v.map(null), v.filter(null).length, v.some(null), v.every(null));
		// Only true itself counts.
		trace(v.filter(function (x:int, i:int, all:Vector.<int>):* { return 1; }).length, v.some(function (x:int, i:int, all:Vector.<int>):* { return "yes"; }));
		try { v.forEach(odd, this); } catch (e:TypeError) { trace(e.message); }
		trace(v.map(twice, this));
		try { v.forEach(function (x:int):void {}); } catch (e:ArgumentError) { trace("ArgumentError"); }

		trace(v.sort(function (a:int, b:int):Number { return a - b; }));
		trace(v.sort(descending));
		var unsorted:Vector.<int> = new <int>[3, 1, 2];
		try { unsorted.sort(function (a:int, b:int):Number { throw new Error("stop"); }); } catch (e:Error) { trace(e.message, unsorted); }

		try { unsorted.sort("x"); } catch (e:TypeError) { trace(e.message); }
		trace(unsorted.sort(8));
		trace(unsorted.sort(4 | 16), new <int>[2, 1, 2].sort(4 | 16));
		var names:Vector.<String> = new <String>["b", "C", "a"];
		trace(names.sort(0));
		trace(names.sort(1));
		trace(names.sort(3));
		var numbers:Vector.<Number> = new <Number>[10, 9, 1.5];
		trace(numbers.sort(0));
		trace(numbers.sort(16));
		trace(numbers.sort(18));

		var items:Vector.<Item> = new <Item>[new Item("a"), new Item("b")];
		var copies:Vector.<Item> = items.map(function (it:Item, i:int, all:Vector.<Item>):* { return new Item(it.name + i); });
		trace(copies[0].name, copies[1].name, copies.length);
		try { items.map(function (it:Item, i:int, all:Vector.<Item>):* { return "x"; }); } catch (e:TypeError) { trace("TypeError"); }
		trace(items.filter(function (it:Item, i:int, all:Vector.<Item>):Boolean { return it.name == "b"; })[0].name);

		// The length is read once: what is added is not reached; what is taken out is out of range.
		var count:int = 0;
		var grows:Vector.<int> = new <int>[1, 2, 3];
		grows.forEach(function (x:int, i:int, all:Vector.<int>):void { count++; if (all.length < 5) all.push(9); });
		trace(count, grows);
		var shrinks:Vector.<int> = new <int>[1, 2, 3];
		try { shrinks.forEach(function (x:int, i:int, all:Vector.<int>):void { all.pop(); }); } catch (e:RangeError) { trace(e.message); }
	}
	private function odd(x:int, i:int, all:Vector.<int>):Boolean { return x % 2 == 1; }
	private function twice(x:int, i:int, all:Vector.<int>):int { return x * 2; }
	private static function descending(a:int, b:int):Number { return b - a; }
} }
class Item { public var name:String; public function Item(p_name:String) { name = p_name; } }
