package {
	public class Derived extends Base {
		// Declared in a later file than Base's field of the same kind: a class may name any other.
		public var partner:Derived;
		override public function _physicsProcess(delta:Number):void { steps += 100; }
	}
}
