package {
	public class err_vector_fixed_pop {
		public function err_vector_fixed_pop() {
			trace("before");
			var v:Vector.<String> = new Vector.<String>(1, true);
			v.pop();
			trace("after");
		}
	}
}
