package {
	// A private method called on a missing object from a static function is an error at the call, not a
	// method running with no object.
	public class err_static {
		private static var instance:err_static;
		private var count:int = 0;
		public function err_static() {
			trace("start");
			trace(reset());
			trace("not reached");
		}
		public static function reset():int {
			instance.clear();
			return 1;
		}
		private function clear():void {
			trace("clear ran");
			count = 0;
		}
	}
}
