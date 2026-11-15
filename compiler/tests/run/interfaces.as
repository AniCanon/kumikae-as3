package {
	// Interfaces: a call through one finds the method on whatever class the object is, by name.
	public class interfaces {
		private var held:IShape;

		public function interfaces() {
			var shapes:Vector.<IShape> = new <IShape>[new Square(2), new Circle(1), new Cube(3)];
			for each (var shape:IShape in shapes) {
				trace(shape.name, shape.area(), describe(shape));
			}
			var s:IShape = new Square(3);
			trace(s.area(), s.scaled(), s.scaled(10));
			s.name = "big";
			trace(s.name, s.sides);
			s.sides += 1;
			trace(s.sides);
			held = new Circle(2);
			trace(held.name, total(shapes));
			// Several interfaces, and one that extends two.
			var cube:Cube = new Cube(2);
			var solid:ISolid = cube;
			var named:INamed = cube;
			trace(solid.volume(), solid.area(), named.name, solid.name);
			trace(cube is IShape, cube is ISolid, cube is INamed, new Square(1) is ISolid, s is INamed);
			trace(s as ISolid, (shapes[2] as ISolid).volume(), held as IShape == held);
			var nothing:IShape = null;
			trace(nothing is IShape, nothing as IShape);
			// A method of an interface as a function value.
			var area:Function = s.area;
			trace(area());
			// Through an untyped value: checked against the interface on the way in.
			var any:* = cube;
			var back:ISolid = any;
			trace(back.volume());
			var square:* = new Square(1);
			try {
				var wrong:ISolid = square;
				trace(wrong);
			} catch (e:TypeError) {
				trace("TypeError");
			}
			// Inherited implementations, and an override in a subclass.
			var sub:IShape = new BigSquare(2);
			trace(sub.area(), sub.name, sub is IShape);
		}
		private function describe(p_shape:IShape):String {
			return p_shape.name + "/" + p_shape.sides;
		}
		private function total(p_shapes:Vector.<IShape>):Number {
			var sum:Number = 0;
			for each (var shape:IShape in p_shapes) {
				sum += shape.area();
			}
			return sum;
		}
	}
}

interface INamed {
	function get name():String;
	function set name(p_name:String):void;
}

interface IShape extends INamed {
	function area():Number;
	function scaled(p_by:Number = 2):Number;
	function get sides():int;
	function set sides(p_sides:int):void;
}

interface ISolid extends IShape, INamed {
	function volume():Number;
}

class Square implements IShape {
	protected var size:Number;
	private var own:String = "square";
	private var count:int = 4;
	public function Square(p_size:Number) { size = p_size; }
	public function area():Number { return size * size; }
	public function scaled(p_by:Number = 2):Number { return area() * p_by; }
	public function get name():String { return own; }
	public function set name(p_name:String):void { own = p_name; }
	public function get sides():int { return count; }
	public function set sides(p_sides:int):void { count = p_sides; }
}

class BigSquare extends Square {
	public function BigSquare(p_size:Number) { super(p_size); }
	override public function area():Number { return super.area() * 100; }
}

class Circle implements IShape {
	private var radius:Number;
	public function Circle(p_radius:Number) { radius = p_radius; }
	public function area():Number { return 3 * radius * radius; }
	public function scaled(p_by:Number = 2):Number { return area() * p_by; }
	public function get name():String { return "circle"; }
	public function set name(p_name:String):void {}
	public function get sides():int { return 0; }
	public function set sides(p_sides:int):void {}
}

class Cube extends Square implements ISolid, INamed {
	public function Cube(p_size:Number) {
		super(p_size);
		name = "cube";
		sides = 6;
	}
	override public function area():Number { return 6 * size * size; }
	public function volume():Number { return size * size * size; }
}
