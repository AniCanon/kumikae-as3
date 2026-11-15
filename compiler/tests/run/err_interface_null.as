package {
	// A call through an interface on a missing object is reported after its arguments, as any call is.
	public class err_interface_null {
		public function err_interface_null() {
			var shape:IShape = null;
			shape.grow(note());
		}
		private function note():Number {
			trace("argument first");
			return 2;
		}
	}
}

interface IShape {
	function grow(p_by:Number):void;
}
