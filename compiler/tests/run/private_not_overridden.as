package {
	public class private_not_overridden {
		public function private_not_overridden() {
			var child:Child = new Child();
			child.callOwn();
			child.own();
			Base.made();
			child.made();
		}
	}
}

class Base {
	public function Base() { trace("Base constructor"); }
	private function own():void { trace("Base own"); }
	public function callOwn():void { own(); }
	public static function made():void { trace("Base made"); }
}

class Child extends Base {
	public function Child() { super(); trace("Child constructor"); }
	public function own():void { trace("Child own"); }
	public function made():void { trace("Child made"); }
}
