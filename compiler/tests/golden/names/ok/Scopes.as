package {
	public class Scopes {
		private var total:int;

		public function run(limit:int, ...rest):int {
			early = limit;
			var early:int;
			for (var i:int = 0; i < limit; i++) {
				var inside:int = i * 2;
				total += inside;
			}
			try {
				later(rest.length);
			} catch (problem:Error) {
				trace(problem.message, arguments.length);
			}
			function later(count:int):void {
				total += count + early;
			}
			var again:Function = function countdown(n:int):int {
				return n > 0 ? countdown(n - 1) : total;
			};
			var total:Number = again(3);
			return int(total) + this.total;
		}
	}
}
