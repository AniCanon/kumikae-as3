package {
	public class classes {
		public static var made:int = 0;
		public static const LIMIT:int = 3;
		private var total:Number = 10;
		private var label:String;

		public function classes() {
			trace("start", made, LIMIT, total, label);
			var dog:Animal = new Dog("Rex");
			var cat:Animal = new Cat();
			trace(dog.speak(), cat.speak());
			trace(dog.describe());
			trace(cat.describe());
			trace(dog is Dog, dog is Cat, dog is Animal, cat as Dog, (cat as Cat) == cat);
			trace(Animal.count, made);
			for (var i:int = 0; i < LIMIT; i++) {
				total += bump(i);
			}
			trace(total, this.total, add(1), add(1, 5));
			trace(dog, cat);
			var none:Animal;
			trace(none, none == null, dog != null);
		}

		private function bump(by:int):int {
			made++;
			return by * 2;
		}

		public function add(a:int, b:int = 10):int {
			return a + b;
		}
	}
}

class Animal {
	public static var count:int;
	public var name:String = "nameless";
	protected var legs:int = 4;

	public function Animal(name:String = null) {
		count++;
		if (name != null) {
			this.name = name;
		}
	}
	public function speak():String {
		return "...";
	}
	public function describe():String {
		return name + " says " + speak() + " on " + legs + " legs";
	}
}

class Dog extends Animal {
	public function Dog(name:String) {
		super(name);
	}
	override public function speak():String {
		return "Woof";
	}
}

class Cat extends Animal {
	override public function speak():String {
		return "Meow";
	}
	override public function describe():String {
		return "A cat: " + super.describe();
	}
}
