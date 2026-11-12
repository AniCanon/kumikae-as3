package {
	public class err_throw {
		public function err_throw() {
			var plain:Error = new Error("plain");
			trace(plain, plain.message, new Error(), new RangeError("out of range"), plain is Error, new Problem("p", 7) is Error);
			var problem:Problem = new Problem("it broke", 42);
			trace(problem, problem.message, problem.code);
			check(3);
			check(-1);
			trace("not reached");
		}
		private function check(value:int):void {
			if (value < 0) {
				throw new Problem("negative: " + value, value);
			}
			trace("fine", value);
		}
	}
}
class Problem extends Error {
	public var code:int;
	public function Problem(message:String, code:int) {
		super(message);
		this.code = code;
	}
}
