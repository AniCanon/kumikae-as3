package {
	import flash.utils.Dictionary;

	// A loop variable with a type, over a collection whose elements are untyped: each element is stored as
	// an assignment stores it, checked against the variable's type.
	public class for_each_typed {
		public function for_each_typed() {
			var list:Array = [new Enemy("a"), new Boss("b"), null];
			for each (var enemy:Enemy in list) {
				trace(enemy ? enemy.name : "none", enemy is Boss);
			}
			var table:Object = { first: new Enemy("c") };
			for each (var held:Enemy in table) {
				trace(held.name);
			}
			var keyed:Dictionary = new Dictionary();
			keyed["k"] = new Boss("d");
			for each (var boss:Boss in keyed) {
				trace(boss.name, boss.rank);
			}
			for each (var each:Function in [function():String { return "called"; }]) {
				trace(each());
			}
			for each (var kind:Class in [String, Enemy]) {
				trace(kind);
			}
			for each (var text:String in ["a", 1, null]) {
				trace(text);
			}
			for each (var row:Array in [[1, 2], [3]]) {
				trace(row.length);
			}
			var sequence:Vector.<*> = new Vector.<*>();
			sequence.push(new <int>[1, 2], new <int>[3]);
			for each (var numbers:Vector.<int> in sequence) {
				trace(numbers);
			}
			var kept:Enemy;
			for each (kept in list) {
			}
			trace(kept);
			// What is not of the variable's type is AS3's coercion error.
			try {
				for each (var wrong:Boss in list) {
					trace(wrong.name);
				}
			} catch (e:TypeError) {
				trace(e.errorID);
			}
			try {
				for each (var number:Enemy in [1]) {
				}
			} catch (e:TypeError) {
				trace(e.message);
			}
		}
	}
}
class Enemy {
	public var name:String;
	public function Enemy(name:String) { this.name = name; }
}
class Boss extends Enemy {
	public var rank:int = 9;
	public function Boss(name:String) { super(name); }
}
