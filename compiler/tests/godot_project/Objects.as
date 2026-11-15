package {
	import flash.utils.Dictionary;
	import godot.*;

	// AS3's Objects and Dictionaries between AS3 and Godot: objects.gd does the same in GDScript.
	public class Objects extends Node2D {
		public function literalToGodot(target:Node):void {
			target.setMeta("o", { a: 1, b: "two", list: [1, { c: 2.5 }], at: new Vector2(1, 2) });
		}
		public function dictionaryToGodot(target:Node):void {
			var d:Dictionary = new Dictionary();
			d["x"] = 1;
			d[2] = "two";
			var untyped:* = d;
			target.setMeta("d", untyped);
		}
		public function insideArray(target:Node):void {
			var list:* = [{ k: "v" }, [{ n: null }]];
			target.setMeta("list", list);
		}
		public function made():Object {
			return { made: true, count: 3 };
		}
		public function queried():String {
			var client:HTTPClient = new HTTPClient();
			return client.queryStringFromDict({ a: 1, b: "x y" });
		}
		public function fromGodot(d:Dictionary):String {
			var out:String = "";
			for (var k:String in d) {
				out += k + "=" + d[k] + ";";
			}
			return out;
		}
		public function stored(target:Node):String {
			var d:Dictionary = target.getMeta("table") as Dictionary;
			var untyped:* = target.getMeta("table");
			var kept:Object = target.getMeta("table");
			return d.name + "/" + (untyped is Dictionary) + "/" + (kept is Dictionary);
		}
		public function holdsItself(target:Node):void {
			var o:Object = { name: "loop" };
			o.self = o;
			try {
				target.setMeta("loop", o);
			} finally {
				delete o.self;
			}
		}
		public function withProperties(target:Node):void {
			var list:Array = [1];
			list.extra = true;
			var untyped:* = list;
			target.setMeta("props", untyped);
		}
		public function loopGodot(target:Node):int {
			var untyped:* = target.getMeta("table");
			var seen:int = 0;
			for (var k:* in untyped) {
				seen++;
			}
			return seen;
		}
		public function plainInside(target:Node):void {
			target.setMeta("plain", { thing: new Unheld() });
		}
	}
}

class Unheld {
}
