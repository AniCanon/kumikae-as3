package {
	public class err_vector_fixed_length {
		public function err_vector_fixed_length() {
			trace("before");
			var v:Vector.<int> = new Vector.<int>(2, true);
			v.length = 3;
			trace("after");
		}
	}
}
