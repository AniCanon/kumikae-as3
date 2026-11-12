package game {
	public class Counter {
		public static var made:int = 0;
		public var total:int;
		public function Counter(start:int) { total = start; made++; }
		public function add(amount:int):void { total += amount; }
	}
}
