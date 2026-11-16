package { public class locale_text {
	// toLocaleString() of an Array and a Vector, typed and by name: each element's toLocaleString(), with
	// commas between; null and undefined left out of both (avmplus's Array.as and VectorImpl.as, from
	// recollection: `if (x != null)` and `if (x !== undefined && x !== null)`).
	public function locale_text() {
		var a:Array = [1, "two", null, undefined, 3.5, true];
		trace(a.toLocaleString(), "|", a.toString());
		var v:Vector.<int> = new <int>[3, 2, 1];
		trace(v.toLocaleString());
		var names:Vector.<String> = new <String>["a", null, "c"];
		trace(names.toLocaleString(), "|", names.join(","));
		var things:Vector.<Object> = new <Object>[1, null, "x"];
		trace(things.toLocaleString());
		var any:Vector.<*> = new <*>[undefined, 2, null];
		trace(any.toLocaleString());
		trace([].toLocaleString() == "", new Vector.<Number>().toLocaleString() == "");
		// One inside another speaks in its locale words too.
		trace([[1, null, 2], new <String>[null, "s"], [[3]]].toLocaleString());
		// An element's own toLocaleString() is what is asked of it; without one, its toString().
		trace([new Spoken(), new Plain(), new Texted()].toLocaleString());
		trace(new <Spoken>[new Spoken(), null, new Spoken()].toLocaleString());
		// By name, on an untyped value: the same.
		var held:* = a;
		var heldVector:* = names;
		trace(held.toLocaleString(), "|", heldVector.toLocaleString());
		var mixed:* = [new Spoken(), 1];
		trace(mixed.toLocaleString());
		// A null one is AS3's error.
		var none:Array = null;
		try { none.toLocaleString(); } catch (e:TypeError) { trace("caught", e.errorID); }
		// One that holds itself ends as its text does.
		var loop:Array = [1];
		loop.push(loop);
		try { loop.toLocaleString(); } catch (e:Error) { trace("caught", e.errorID); }
	}
} }
class Spoken { public function toLocaleString():String { return "spoken"; } public function toString():String { return "plain"; } }
class Plain {}
class Texted { public function toString():String { return "texted"; } }
