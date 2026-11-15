package {
	// A missing object is reported when the setter would be called: after the value is worked out.
	public class err_accessor_null {
		public function err_accessor_null() {
			var box:Box = null;
			box.size = note();
		}
		private function note():int {
			trace("value first");
			return 1;
		}
	}
}

class Box {
	public function get size():int { return 0; }
	public function set size(p_value:int):void {}
}
