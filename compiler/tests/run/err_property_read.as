package {
	public class err_property_read {
		public function err_property_read() {
			var x:* = new Box();
			trace(x.size);
			trace(x.weight);
			trace("not reached");
		}
	}
}
class Box {
	public var size:int = 2;
}
