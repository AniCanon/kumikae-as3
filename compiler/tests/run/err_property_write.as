package {
	public class err_property_write {
		public function err_property_write() {
			var x:Object = new Box();
			x.size = 3;
			trace(x.size);
			x.weight = 4;
			trace("not reached");
		}
	}
}
class Box {
	public var size:int = 2;
}
