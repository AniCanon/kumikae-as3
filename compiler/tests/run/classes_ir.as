package {
	public class classes_ir {
		public function classes_ir() {
			var shape:Shape = new Square(2);
			trace(shape.area(), Shape.count, shape is Square);
		}
	}
}

class Shape {
	public static var count:int = 0;
	public function Shape() { count++; }
	public function area():Number { return 0; }
}

class Square extends Shape {
	private var side:Number;
	public function Square(p_side:Number) { super(); side = p_side; }
	override public function area():Number { return side * side; }
}
