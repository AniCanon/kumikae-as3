// An object of a sealed class of the program, reached by name through an untyped value: its public fields
// and methods, and Flash's ReferenceErrors for anything else.
package {
	public class object_sealed {
		public function object_sealed() {
			var x:* = new Thing("rex");
			trace(x.name, x["count"], x.speak(), x["speak"]("loudly"));
			x.name = "max";
			x.count = "7"; // Coerced as a store into an int is.
			x.count += 1;
			trace(x.name, x.count, typeof x.count);
			var f:Function = x.speak;
			trace(f(), f == x.speak);
			trace("name" in x, "speak" in x, "secret" in x, x.hasOwnProperty("count"), x.hasOwnProperty("nothing"), "toString" in x);
			trace(delete x.name, delete x.nothing, x.name);
			var keys:int = 0;
			for (var k:* in x) {
				keys++;
			}
			for each (var v:* in x) {
				keys++;
			}
			trace(keys, String(x), x.toString());
			var o:Object = new Sub("kid");
			trace(o.name, o.speak(), o.extra, o.inherited());
			try {
				trace(x.secret);
			} catch (e:ReferenceError) {
				trace(e.errorID, e.message);
			}
			try {
				x.invented = 1;
			} catch (e:ReferenceError) {
				trace(e.errorID, e.message);
			}
			try {
				x.speak = null;
			} catch (e:ReferenceError) {
				trace(e.errorID, e.message);
			}
			try {
				x.hidden();
			} catch (e:ReferenceError) {
				trace(e.errorID, e.message);
			}
			try {
				x.pet = "not a thing";
			} catch (e:TypeError) {
				trace(e.errorID, e.message);
			}
			try {
				x.name();
			} catch (e:TypeError) {
				trace(e.errorID, e.message);
			}
			var error:* = new RangeError("too far", 7);
			trace(error.message, error.name, error.errorID);
			var nothing:* = null;
			try {
				nothing.anything;
			} catch (e:TypeError) {
				trace(e.errorID);
			}
			var missing:*;
			try {
				missing.call(1, trace("arguments first"));
			} catch (e:TypeError) {
				trace(e.errorID);
			}
		}
	}
}

class Thing {
	public var name:String;
	public var count:int = 3;
	public var pet:Thing;
	private var secret:String = "hidden";
	public function Thing(p_name:String) {
		name = p_name;
	}
	public function speak(how:String = "softly"):String {
		return name + " speaks " + how;
	}
	private function hidden():void {}
	public function inherited():String {
		return "from Thing";
	}
}

class Sub extends Thing {
	public var extra:Boolean = true;
	public function Sub(p_name:String) {
		super(p_name);
	}
	override public function speak(how:String = "softly"):String {
		return "sub " + super.speak(how);
	}
}
