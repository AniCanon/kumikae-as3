// A method of an Array, a String or a number reached by its name through an untyped value is not run yet:
// it says so, as the program runs, rather than giving undefined.
package {
	public class err_property_unsupported {
		public function err_property_unsupported() {
			var text:* = "abc";
			trace(text.length);
			try {
				text.charAt(0);
			} catch (e:Error) {
				trace(e.message);
			}
			var list:* = [1, 2];
			trace(list.length, list[1], list.toString(), list.hasOwnProperty(0));
			list.push(3);
			trace("not reached");
		}
	}
}
