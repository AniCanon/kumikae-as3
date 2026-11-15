package {
	// How an interface reads in the IR: a class with no objects, each class's table, CallInterface.
	public class interfaces_ir {
		public function interfaces_ir() {
			var shape:Shape = new Square();
			trace(shape.area(), shape.name, shape is Named);
		}
	}
}

interface Named {
	function get name():String;
}

interface Shape extends Named {
	function area():Number;
}

class Square implements Shape {
	public function area():Number { return 4; }
	public function get name():String { return "square"; }
}
