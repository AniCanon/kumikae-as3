package {
	// A hole in an Array (an index never written, or deleted) is not an element that holds undefined, as
	// avmplus has it (core/ArrayObject.cpp: a hole is atomNotFound in the dense list). `for in`, `for each`,
	// `in` and hasOwnProperty pass over holes only (nextNameIndex, hasUintProperty). Reading one gives
	// undefined, so forEach, map, filter, some, every and indexOf see every index (ArrayClass.cpp's
	// generic_forEach and the rest read getUintProperty(i) for each i), join writes nothing for one, and
	// JSON writes null. sort puts what is defined first, then the elements that hold undefined, then the
	// holes, and keeps the length (ArraySort). shift, unshift, splice, concat and reverse keep a hole a
	// hole (the dense paths); slice's copy holds undefined there (generic_slice sets every index).
	public class array_holes {
		private function keys(p_array:Array):String {
			var out:Array = [];
			for (var key:String in p_array) {
				out.push(key);
			}
			return "[" + out.join(",") + "]";
		}
		private function values(p_array:Array):String {
			var out:Array = [];
			for each (var value:* in p_array) {
				out.push(String(value));
			}
			return "[" + out.join(",") + "]";
		}
		public function array_holes() {
			var stored:Array = [1, undefined, 2];
			trace(keys(stored), values(stored), stored.length);
			var made:Array = new Array(3);
			trace(keys(made), values(made), made.length, made[1], made[1] === undefined);
			made[1] = "x";
			trace(keys(made), values(made), 0 in made, 1 in made, made.hasOwnProperty(0), made.hasOwnProperty("1"));
			delete stored[1];
			trace(keys(stored), values(stored), stored.length, 1 in stored, stored[1]);
			stored[1] = undefined;
			trace(keys(stored), 1 in stored, stored.hasOwnProperty(1));
			var sparse:Array = [];
			sparse[3] = 7;
			trace(keys(sparse), sparse.length, sparse.join("-"), String(sparse), sparse.toLocaleString());
			sparse.length = 6;
			trace(keys(sparse), sparse.length, 5 in sparse);
			trace(sparse.indexOf(undefined), sparse.lastIndexOf(undefined), sparse.indexOf(7), [1, undefined].indexOf(undefined));
			var calls:int = 0;
			sparse.forEach(function (p_item:*, p_index:int, p_all:Array):void { calls++; });
			var mapped:Array = sparse.map(function (p_item:*, p_index:int, p_all:Array):* { return p_item; });
			var kept:Array = sparse.filter(function (p_item:*, p_index:int, p_all:Array):Boolean { return p_item === undefined; });
			trace(calls, keys(mapped), keys(kept), kept.length);
			trace(sparse.some(function (p_item:*, p_index:int, p_all:Array):Boolean { return p_item === undefined; }),
					sparse.every(function (p_item:*, p_index:int, p_all:Array):Boolean { return p_item !== undefined; }));
			trace(JSON.stringify(sparse), JSON.stringify([1, undefined, null]));
			var mixed:Array = [3, undefined, 1];
			mixed[5] = 2;
			mixed.sort();
			trace(keys(mixed), values(mixed), mixed.length);
			var numbers:Array = new Array(4);
			numbers[2] = 5;
			numbers[0] = 9;
			numbers.sort(Array.NUMERIC);
			trace(keys(numbers), values(numbers), numbers.length);
			var moved:Array = [1, 2, 3];
			delete moved[1];
			trace(keys(moved.concat([4])), keys(moved.slice()), keys(moved.concat()), moved.slice()[1]);
			moved.reverse();
			trace(keys(moved), values(moved));
			moved.unshift(0);
			trace(keys(moved), values(moved), moved.length);
			trace(moved.shift(), keys(moved), moved.shift(), keys(moved), moved.shift(), keys(moved));
			var cut:Array = [1, 2, 3, 4, 5];
			delete cut[3];
			cut.splice(0, 1);
			trace(keys(cut), values(cut), cut.length);
			var last:Array = [1, 2];
			last.length = 4;
			trace(last.pop(), last.length, keys(last));
			last.push(9);
			trace(keys(last), values(last), last.length);
			var held:* = last;
			trace(2 in held, 3 in held, held.hasOwnProperty(2), held.propertyIsEnumerable(2), held.propertyIsEnumerable(3));
			var copied:Array = [last[2], last[3]];
			trace(keys(copied), copied[0] === undefined);
			// What a replacer is handed for a hole is undefined, and stays so wherever it is put.
			var handed:Array = [];
			JSON.stringify(new Array(2), function (p_key:String, p_value:*):* {
				if (p_key != "") {
					handed.push(p_value);
				}
				return p_value;
			});
			trace(keys(handed), handed.length);
			var typed:Vector.<*> = new Vector.<*>(2);
			var seen:int = 0;
			for each (var each:* in typed) {
				seen++;
			}
			trace(seen, typed.indexOf(undefined));
		}
	}
}
