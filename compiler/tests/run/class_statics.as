package {
	// A class held in a variable gives its static members by name, as the class named does: methods,
	// fields, constants, getters and setters, a base class's too only through the base (AS3's statics are
	// not inherited). What it does not declare is ReferenceError #1069 to read, #1056 to write.
	public class class_statics {
		public function class_statics() {
			var c:Class = Maker;
			var made:* = c.create(3);
			trace(made.size, c.made, c.LIMIT, c.label, c["create"](4).size, c.made);
			c.made = 10;
			c.label = "set";
			trace(Maker.made, Maker.label, c.made, c.describe());
			var f:Function = c.create;
			trace(f(7).size, c.made, f == Maker.create);
			var held:* = c;
			held.made += 5;
			trace(held.made, held.count(), "create" in held, held.hasOwnProperty("made"), "missing" in held);
			var sub:Class = Smaller;
			trace(sub.own(), new sub().size);
			try {
				trace(sub.create(1));
			} catch (e:ReferenceError) {
				trace(e.errorID);
			}
			try {
				trace(held.missing);
			} catch (e:ReferenceError) {
				trace(e.errorID);
			}
			try {
				held.missing = 1;
			} catch (e:ReferenceError) {
				trace(e.errorID);
			}
			try {
				held.LIMIT = 1;
			} catch (e:ReferenceError) {
				trace(e.errorID);
			}
			try {
				held.secret();
			} catch (e:ReferenceError) {
				trace(e.errorID);
			}
			try {
				held.create();
			} catch (e:ArgumentError) {
				trace(e.errorID);
			}
		}
	}
}
class Maker {
	public static const LIMIT:int = 99;
	public static var made:int = 0;
	private static var text:String = "none";
	public var size:int;
	public function Maker(p_size:int = 1) {
		size = p_size;
	}
	public static function create(p_size:int):Maker {
		made++;
		return new Maker(p_size);
	}
	public static function count():int {
		return made;
	}
	public static function describe():String {
		return "made " + made + " " + text;
	}
	public static function get label():String {
		return text;
	}
	public static function set label(p_text:String):void {
		text = p_text;
	}
	private static function secret():void {
	}
}
class Smaller extends Maker {
	public static function own():String {
		return "own";
	}
}
