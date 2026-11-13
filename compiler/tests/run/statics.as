package {
	// A class's static fields get their values the first time the class is used, as in Flash: not before
	// the program starts, and not at all for a class nothing uses.
	public class statics {
		public static var log:String = "";
		public function statics() {
			trace("start", log);
			trace(Config.limit);
			trace(log);
			trace(Config.limit, Defaults.limit);
			trace(log);
			var made:Lazy = new Lazy();
			trace(log);
			trace(Sub.own, Base.shared, log);
			trace(Helper.twice(4), log);
			trace(Loop1.a, Loop2.b);
			trace(log);
			// A class that extends Error is a class like any other.
			var problem:Problem = new Problem("p");
			trace(Problem.made, log);
		}
		public static function note(text:String, value:int):int {
			log += text + " ";
			return value;
		}
	}
}
// Declared before the class it reads from: with first-use initialisation the order does not matter.
class Config {
	public static var limit:int = Defaults.limit * 2;
	public static var seen:int = statics.note("Config", 1);
}
class Defaults {
	public static var limit:int = statics.note("Defaults", 50);
}
class Lazy {
	public static var made:int = statics.note("Lazy", 0);
}
class Never {
	public static var used:int = statics.note("Never", 0);
}
class Base {
	public static var shared:int = statics.note("Base", 7);
}
class Sub extends Base {
	public static var own:int = statics.note("Sub", 8);
}
class Helper {
	public static var ready:int = statics.note("Helper", 1);
	public static function twice(n:int):int { return n * 2; }
}
// Two classes that read each other: the one that started first sees the other's value; the other sees a
// default, because the first is still being set up.
class Problem extends Error {
	public static var made:int = statics.note("Problem", 5);
	public function Problem(message:String) { super(message); }
}
class Loop1 {
	public static var a:int = Loop2.b + 1;
}
class Loop2 {
	public static var b:int = Loop1.a + 10;
}
