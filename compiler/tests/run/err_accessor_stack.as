package {
	// A getter's frame reads as Flash writes it: `Class/get name()`.
	public class err_accessor_stack {
		public function err_accessor_stack() {
			var box:Box = new Box();
			trace(box.size);
		}
	}
}

class Box {
	public function get size():int {
		throw new Error("no size");
	}
}
