package { public class class_values {
	// A class is a value: kept in a variable, compared, tested against, and made objects of.
	public function class_values() {
		var c:Class = Shape;
		var held:* = Circle;
		trace(c, held, c == Shape, c === held, held == Circle, typeof c, c is Class, held is Class, String(c), "" + held);
		var shape:Shape = new Circle(2);
		var any:* = shape;
		trace(shape is c, shape is held, any is held, new Shape() is held, null is c, shape as c, (new Shape() as held) == null, any as held);
		trace(shape instanceof c, any instanceof held, new Shape() instanceof held, shape instanceof named(), 3 instanceof Number);
		// new of a class held in a variable: its constructor's arguments converted and counted as a call's are.
		var made:* = new held(3);
		var plain:Object = new c();
		trace(made, plain, made is Circle, new (pick(true))(5), new (pick(false))());
		try { new held(); } catch (e:ArgumentError) { trace(e.errorID); }
		try { new held("x", 2); } catch (e:ArgumentError) { trace(e.errorID); }
		// The classes of the library, and of numbers and text.
		var kinds:Array = [int, uint, Number, String, Boolean, Array, Object, Function, Error, Vector.<int>, Named];
		trace(kinds.join(" "));
		trace(3 is kinds[0], -3 is kinds[1], 2.5 is kinds[2], "s" is kinds[3], true is kinds[4], [] is kinds[5], {} is kinds[6], pick is kinds[7],
				new Error() is kinds[8], new <int>[] is kinds[9], shape is kinds[10], 2.5 is kinds[0], null is kinds[6]);
		var list:* = new kinds[5](3);
		var object:* = new kinds[6]();
		var numbers:* = new kinds[9](2, true);
		trace(list.length, object is Object, numbers.length, numbers.fixed, numbers is Vector.<int>);
		var dates:Class = Date;
		var patterns:Class = RegExp;
		var errors:Class = RangeError;
		var failed:* = new errors("too far", 7);
		trace(new dates(0).getTime(), new dates(2024, 1, 29).getMonth(), new patterns("a+", "g").test("caat"), new patterns(/b/i).source,
				failed.message, failed.errorID, failed is RangeError, failed is Error, new String == "", new (int)("12") + 1);
		// Class(x): x if it is a class, null for null, else AS3's TypeError #1034.
		trace(Class(held) === Circle, Class(null), Class(c) == Shape);
		try { Class(shape); } catch (e:TypeError) { trace(e.errorID); }
		// What is no class, where one is wanted.
		var nothing:Class = null;
		try { shape is nothing; } catch (e:TypeError) { trace(e.errorID); }
		try { new nothing(); } catch (e:TypeError) { trace(e.errorID); }
		var notClass:* = 5;
		try { shape is notClass; } catch (e:TypeError) { trace(e.errorID); }
		try { new notClass(); } catch (e:TypeError) { trace(e.errorID); }
		try { new kinds[10](); } catch (e:Error) { trace(e.errorID > 0); }
		// As a key, and in a Vector of classes.
		var registry:Object = {};
		var classes:Vector.<Class> = new <Class>[Shape, Circle];
		trace(classes.indexOf(Circle), classes[0] == c, classes.length);
	}
	private function named():Class { return Shape; }
	private function pick(round:Boolean):Class { return round ? Circle : Shape; }
} }
interface Named {}
class Shape implements Named {
	public static var made:int = count();
	private static function count():int { trace("Shape set up"); return 0; }
	public function toString():String { return "a shape"; }
}
class Circle extends Shape {
	private var radius:Number;
	public function Circle(radius:Number) { this.radius = radius; }
	override public function toString():String { return "a circle of " + radius; }
}
