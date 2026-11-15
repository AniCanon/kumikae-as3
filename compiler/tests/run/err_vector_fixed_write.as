package {
	public class err_vector_fixed_write {
		public function err_vector_fixed_write() {
			trace("before");
			var v:Vector.<int> = new Vector.<int>(2, true);
			v[1] = 5;
			trace(v);
			v[2] = 6;
			trace("after");
		}
	}
}
