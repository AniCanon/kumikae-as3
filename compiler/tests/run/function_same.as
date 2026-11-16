package {
	// Two function values are equal when they are one method of one object. A method of the library read
	// from a value (`list.push`) is bound to that value and its name, and is no other function's equal.
	public class function_same {
		private var count:int = 0;
		public function function_same() {
			var list:* = [3, 1, 2];
			var method:Function = bump;
			var captured:int = 4;
			var closure:Function = function ():int { return captured + count; };
			trace(list.push == method, method == list.push, list.push == closure, closure == list.push);
			trace(list.push == list.push, list.push == list.pop, list.push == [3, 1, 2].push, method == bump, closure == closure);
			var held:Array = [list.push, method];
			trace(held.indexOf(bump), held.indexOf(list.push), held.indexOf(closure), held.lastIndexOf(list.pop));
			var text:* = "abc";
			trace(text.charAt == list.push, text.charAt == text.charAt, held.indexOf(text.charAt));
		}
		private function bump():int {
			return ++count;
		}
	}
}
