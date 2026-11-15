package { public class vector_untyped {
	public function vector_untyped() {
		var v:Vector.<*> = new <*>[1, "two", 3.5, new Thing()];
		trace(v.length, v);
		v.push(null);
		v.push(undefined);
		trace(v[4], v[5], v.length);
		trace(v.indexOf("two"), v.indexOf(1), v.indexOf("1"), v.lastIndexOf(null));
		trace(typeof v[1], typeof v[0], v[3] is Thing);
		var last:* = v.pop();
		trace(last, new Vector.<*>().pop());
		v[0] = "changed";
		trace(v[0], v.length);
		var w:Vector.<*> = v.map(function (e:*, i:int, all:Vector.<*>):* { return i * 2; });
		trace(w);
		trace(v.filter(function (e:*, i:int, all:Vector.<*>):Boolean { return e is String; }));
		var sum:Number = 0;
		for each (var e:* in w) { sum += e; }
		trace(sum);
		var n:Vector.<*> = new <*>[3, "1", 2];
		trace(n.sort(16), n.slice(1), n.concat(n).length, n.reverse(), n.join("-"));
		var held:Vector.<*> = new <*>[new Thing()];
		var t:Thing = held[0];
		trace(t.name);
	}
} }
class Thing { public var name:String = "kept"; public function toString():String { return "thing"; } }
