package {
	// Rules read from avmplus's source, each where its line is traced:
	// - instanceof (Toplevel::instanceof): a right-hand side that is no class or function, null and
	//   undefined too, is TypeError #1040, looked at before the value; null is in no chain (false);
	//   undefined has no chain to look in (TypeError #1010, toPrototype).
	// - A class called as a function converts its one argument; any other count is ArgumentError #1112
	//   (ClassClosure::call, TypedVectorClassBase::call).
	// - Vector.<T>(x) takes any object: as long as its `length`, of what it holds under each index, each
	//   converted (getLengthProperty, getUintProperty). What is no object is TypeError #1034.
	// - Error #1063 says how many arguments are required, whichever way the count is wrong (MethodEnv's
	//   argcError: "Expected %2" is requiredParamCount).
	// - String's `localeCompare(other:*=void 0)`: nothing given compares with "undefined"; two is #1063.
	// - `new Object(a, b)` takes no notice of what follows the first (ObjectClass::construct).
	public class avmplus_rules {
		public function avmplus_rules() {
			var text:* = "abc";
			var number:* = 5;
			var sealed:* = new Sealed();
			var list:* = [1, 2];
			var none:* = null;
			var missing:*;
			var intVector:* = Vector.<int>;
			var sealedClass:* = Sealed;
			var numberClass:* = Number;
			var steps:Array = [
				function ():* { return 1 instanceof number; },
				function ():* { return sealed instanceof text; },
				function ():* { return sealed instanceof none; },
				function ():* { return missing instanceof missing; },
				function ():* { return [none instanceof Sealed, 1 instanceof Number, "a" instanceof String, sealed instanceof Object, sealed instanceof sealedClass, none instanceof sealedClass]; },
				function ():* { return missing instanceof Sealed; },
				function ():* { return missing instanceof sealedClass; },
				function ():* { return missing instanceof Marked; },
				function ():* { return [Vector.<int>(list), Vector.<int>({length: 2, 0: 7, 1: "8"}), Vector.<int>({length: 2}), Vector.<String>({length: "1", 0: 5})]; },
				function ():* { return Vector.<int>({}); },
				function ():* { return Vector.<int>("ab"); },
				function ():* { return Vector.<int>(none); },
				function ():* { return intVector(); },
				function ():* { return intVector([1], [2]); },
				function ():* { return [intVector([3, "4"]), intVector(intVector([5])).length, sealedClass(sealed) === sealed, sealedClass(none), sealedClass(missing), numberClass("12") + 1]; },
				function ():* { return sealedClass(5); },
				function ():* { return sealedClass(); },
				function ():* { return numberClass(1, 2); },
				function ():* { return text.charAt(1, 2, 3); },
				function ():* { return [text.localeCompare(), text.localeCompare("abc"), "undefined".localeCompare(), "null".localeCompare(null)]; },
				function ():* { return text.localeCompare("a", "b"); },
				function ():* { return list.insertAt(1); },
				function ():* { return list.join(",", 2); },
				function ():* { return sealed.say(1, 2); },
				function ():* { return sealed.say(); },
				function ():* { return sealed.more(); },
				function ():* { return sealed.more(1, 2, 3); },
				function ():* { var o:Object = {a: 1}; var objects:* = Object; return [new objects(o, 2, 3) === o, typeof new objects(null, 2)]; },
				function ():* { var c:* = String; return new c("a", "b"); }
			];
			for (var i:int = 0; i < steps.length; i++) {
				try {
					trace(i, steps[i]());
				} catch (e:Error) {
					trace(i, e.name, e.message);
				}
			}
		}
	}
}
interface Marked {
}
class Sealed {
	public var x:int = 1;
	public function say(p_a:int):String { return "said " + p_a; }
	public function more(p_a:int, p_b:int = 2):String { return "more " + p_a + p_b; }
}
