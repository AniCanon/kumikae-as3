// Array's sortOn, as avmplus's generic sortOn takes its arguments: a field name or an Array of them, and
// options as a number for every field or an Array with one for each.
package {
	public class array_sort_on {
		public function array_sort_on() {
			var people:Array = [
				{ name: "bob", age: 30, city: "Oslo" },
				{ name: "Al", age: 5, city: "Rome" },
				{ name: "cy", age: 30, city: "Bonn" },
				{ name: "Di", age: 100, city: "Oslo" }
			];
			trace(names(people.sortOn("name")));
			trace(names(people.sortOn("name", Array.CASEINSENSITIVE)));
			trace(names(people.sortOn("age")));
			trace(names(people.sortOn("age", Array.NUMERIC)));
			trace(names(people.sortOn("age", Array.NUMERIC | Array.DESCENDING)));
			trace(names(people.sortOn(["city", "age"], [0, Array.NUMERIC | Array.DESCENDING])));
			trace(names(people.sortOn(["age", "name"], Array.NUMERIC)));
			// An options Array of another length is not used.
			trace(names(people.sortOn(["city", "name"], [Array.DESCENDING])));
			var indices:* = people.sortOn("age", Array.NUMERIC | Array.RETURNINDEXEDARRAY);
			trace(indices, names(people));
			trace(people.sortOn("city", Array.UNIQUESORT), names(people));
			trace(people.sortOn("name", Array.UNIQUESORT | Array.CASEINSENSITIVE) == people);
			// A field an element lacks is undefined, which sorts as its text.
			var mixed:Array = [{ k: "w" }, { j: 1 }, { k: "a" }, null];
			trace(mixed.sortOn("k").length, mixed[0].k, mixed[1].k, mixed[2], mixed[3].j);
			var things:Array = [new Thing(3), new Thing(1), new Thing(2)];
			things.sortOn("size", Array.NUMERIC);
			trace(things[0].size, things[1].size, things[2].size);
		}

		private function names(p_list:Array):String {
			var out:String = "";
			for each (var each:Object in p_list) {
				out += each.name + " ";
			}
			return out;
		}
	}
}

class Thing {
	public var size:int;
	public function Thing(p_size:int) {
		size = p_size;
	}
}
