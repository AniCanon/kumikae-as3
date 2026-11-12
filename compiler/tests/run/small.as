package {
	public class small {
		private var total:Number = 1.5;
		public function small() {
			var count:int = 2;
			total += count * 2;
			if (total > 5) {
				trace("total", total, twice(count));
			}
		}
		private function twice(n:int):int {
			return n * 2;
		}
	}
}
