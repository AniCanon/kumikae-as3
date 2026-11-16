package {
	public class vector_search {
		public function vector_search() {
			var words:Vector.<String> = new <String>["a", "b", "a"];
			var numbers:Vector.<int> = new <int>[1, 2, 1];
			// A value of another type is never one of a vector of objects' elements: -1. A vector of numbers
			// makes what it is given a number of its kind first (avmplus's Vector.as: `value:int`).
			trace(words.indexOf("a"), words.lastIndexOf("a"), numbers.indexOf(2));
			var held:Object = "a";
			trace(words.indexOf(held), words.indexOf(5), numbers.lastIndexOf(true));
			var none:Vector.<int> = null;
			try { none.indexOf(true); } catch (e:TypeError) { trace("null vector"); }
		}
	}
}
