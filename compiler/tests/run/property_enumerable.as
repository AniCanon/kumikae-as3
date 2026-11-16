package {
	import flash.utils.Dictionary;

	public class property_enumerable {
	// setPropertyIsEnumerable(name, isEnum): a property of an Object, a Dictionary or an Array (beyond its
	// indices) is hidden from `for in`, `for each`, JSON and propertyIsEnumerable(), and stays there for
	// everything else. On a sealed object it is AS3's ReferenceError #1056 (avmplus, from recollection:
	// only an object with a table of properties has the setting).
	public function property_enumerable() {
		var o:Object = {a: 1, b: 2, c: 3};
		o.setPropertyIsEnumerable("b", false);
		trace(keys(o), values(o), o.b, "b" in o, o.hasOwnProperty("b"), o.propertyIsEnumerable("b"), o.propertyIsEnumerable("a"));
		trace(JSON.stringify(o));
		o.b = 20; // Written, it stays hidden.
		trace(keys(o), o.b);
		o.setPropertyIsEnumerable("b"); // isEnum is true when left out.
		trace(keys(o), o.propertyIsEnumerable("b"));
		o.setPropertyIsEnumerable("missing", false); // Nothing there: nothing happens.
		trace(keys(o), "missing" in o);
		o.setPropertyIsEnumerable("a", false);
		delete o.a;
		o.a = 5; // A new property of the same name is a new one.
		trace(keys(o));
		// Held untyped, and a Dictionary with an object as the key.
		var held:* = o;
		held.setPropertyIsEnumerable("c", false);
		trace(keys(o), held.propertyIsEnumerable("c"));
		var key:Object = {};
		var d:Dictionary = new Dictionary();
		d[key] = "by object";
		d["text"] = "by text";
		var heldDictionary:* = d; // The declared name is a String: an object key goes by name.
		heldDictionary.setPropertyIsEnumerable(key, false);
		trace(values(d), d[key], d.propertyIsEnumerable(key));
		// An Array's own properties; its elements are not hidden.
		var a:Array = [10, 20];
		a.extra = "x";
		a.setPropertyIsEnumerable("extra", false);
		a.setPropertyIsEnumerable("0", false);
		trace(keys(a), a.extra, a.propertyIsEnumerable("extra"), a.propertyIsEnumerable("0"));
		// Not on a sealed object, nor on a value that has no properties of its own.
		var sealed:Sealed = new Sealed();
		try { sealed.setPropertyIsEnumerable("field", false); } catch (e:ReferenceError) { trace(e.message); }
		var number:int = 3;
		try { number.setPropertyIsEnumerable("x", false); } catch (e:ReferenceError) { trace(e.message); }
		var none:Object = null;
		try { none.setPropertyIsEnumerable("x", false); } catch (e:TypeError) { trace("caught", e.errorID); }
	}
	private function keys(of:Object):String { var all:Array = []; for (var k:String in of) { all.push(k); } return "[" + all.join(",") + "]"; }
	private function values(of:Object):String { var all:Array = []; for each (var v:* in of) { all.push(v); } return "[" + all.join(",") + "]"; }
} }
class Sealed { public var field:int = 1; }
