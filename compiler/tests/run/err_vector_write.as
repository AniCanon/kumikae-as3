package {
	public class err_vector_write {
		public function err_vector_write() {
			trace("before");
			var v:Vector.<String> = new Vector.<String>(2);
			v[3] = "x";
			trace("after");
		}
	}
}
