package { public class vector_element_class {
	// A Vector knows the class of its elements, not only that they are objects: an element written by name
	// is checked as a typed store is (TypeError #1034), Vector.<T>(x) converts, and concat takes only
	// vectors of its own type.
	public function vector_element_class() {
		var things:Vector.<Thing> = new <Thing>[new Thing("a")];
		var held:* = things;
		held[0] = new Thing("b");
		held[1] = new Sub("c");
		held[2] = null;
		held[3] = undefined;
		trace(things.join(), things.length);
		try { held[0] = new Other(); } catch (e:TypeError) { trace(e.message); }
		try { held[0] = 5; } catch (e:TypeError) { trace(e.message); }
		try { held[0] = "text"; } catch (e:TypeError) { trace(e.message); }
		trace(things[0]);
		// A vector of vectors, of an interface's, and of functions.
		var grid:Vector.<Vector.<int>> = new <Vector.<int>>[new <int>[1]];
		var heldGrid:* = grid;
		heldGrid[0] = new <int>[7, 8];
		try { heldGrid[0] = new <Number>[1.5]; } catch (e:TypeError) { trace("grid", e.errorID); }
		try { heldGrid[0] = [1]; } catch (e:TypeError) { trace("grid", e.errorID); }
		trace(grid);
		var named:Vector.<Named> = new Vector.<Named>(1);
		var heldNamed:* = named;
		heldNamed[0] = new Thing("named");
		try { heldNamed[0] = new Other(); } catch (e:TypeError) { trace("named", e.errorID); }
		trace(named[0].name());
		var calls:Vector.<Function> = new Vector.<Function>(1);
		var heldCalls:* = calls;
		heldCalls[0] = name;
		try { heldCalls[0] = new Thing("x"); } catch (e:TypeError) { trace("calls", e.errorID); }
		trace(calls[0]());

		// Vector.<T>(x): x itself if it is one; else a new one of an Array's or a Vector's elements, each
		// converted as a store converts it.
		var ints:Vector.<int> = Vector.<int>([1, 2.7, "3", true]);
		trace(ints, ints.length, ints.fixed, Vector.<int>(ints) === ints);
		var numbers:Vector.<Number> = Vector.<Number>(ints);
		numbers[0] = 0.5;
		trace(numbers, ints);
		trace(Vector.<String>(new <int>[1, 2]).join("|"), Vector.<String>([null, undefined, 3]).join("|"));
		trace(Vector.<Thing>([new Thing("x"), null, new Sub("y")]));
		try { Vector.<Thing>([new Other()]); } catch (e:TypeError) { trace(e.message); }
		trace(Vector.<Thing>(new <Sub>[new Sub("s")]), Vector.<*>([1, "a", null]).length, Vector.<Object>(ints));
		var untyped:* = [4, 5];
		trace(Vector.<uint>(untyped), Vector.<Boolean>([0, "x"]), Vector.<Vector.<int>>([ints])[0] === ints);
		try { Vector.<int>(5); } catch (e:TypeError) { trace(e.message); }
		try { Vector.<int>(null); } catch (e:TypeError) { trace(e.message); }
		try { Vector.<int>({length: 2}); } catch (e:TypeError) { trace(e.message); }

		// concat: each argument is a vector of this very type (avmplus's castToThisType), or TypeError #1034.
		var more:Vector.<int> = new <int>[9];
		trace(ints.concat(more, more));
		try { ints.concat(numbers); } catch (e:TypeError) { trace(e.message); }
		try { ints.concat([1, 2]); } catch (e:TypeError) { trace(e.message); }
		var same:* = more;
		trace(ints.concat(same));
	}
	private function name():String { return "called"; }
} }
interface Named { function name():String; }
class Thing implements Named {
	private var label:String;
	public function Thing(label:String) { this.label = label; }
	public function name():String { return label; }
	public function toString():String { return "Thing " + label; }
}
class Sub extends Thing { public function Sub(label:String) { super(label); } }
class Other {}
