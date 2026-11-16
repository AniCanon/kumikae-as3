package { public class by_name_fixed_and_length {
	// By name on an untyped value: a Vector's `fixed`, read and written, and a Function's `length`. A
	// Vector takes no property of its own (ReferenceError #1056, from recollection of avmplus).
	public function by_name_fixed_and_length() {
		var typed:Vector.<int> = new <int>[1, 2];
		var v:* = typed;
		trace(v.fixed, v["fixed"]);
		v.fixed = true;
		trace(v.fixed, typed.fixed);
		try { typed.push(3); } catch (e:RangeError) { trace("caught", e.errorID); }
		v["fixed"] = 0; // Converted as a store into a Boolean is.
		trace(v.fixed, typed.push(3));
		try { v.extra = 1; } catch (e:ReferenceError) { trace(e.message); }
		var a:* = [1, 2];
		trace(a.fixed); // An Array has no such property: undefined.
		var f:* = function (x:int, y:int = 0, ...rest):void {};
		var m:* = method;
		var s:* = fixedOne;
		trace(f.length, m.length, s.length, f["length"]);
		// setPropertyIsEnumerable on a Vector or a Date, which have no property to hide: nothing happens.
		v.setPropertyIsEnumerable("0", false);
		var d:* = new Date(0);
		d.setPropertyIsEnumerable("x", false);
		typed.setPropertyIsEnumerable("0", false);
		trace(typed);
	}
	public function method(a:int, b:String):void {}
	private static function fixedOne(a:int):void {}
} }
