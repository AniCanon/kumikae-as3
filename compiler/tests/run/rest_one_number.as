package {
	// A rest parameter given one number holds that number: it is not `new Array(n)`, which would be a
	// length.
	public class rest_one_number {
		public function rest_one_number() {
			trace(all(5), all(3, 4), all(), all("x"), all(2.5));
		}

		private function all(...rest):String {
			return rest.length + ":" + rest;
		}
	}
}
