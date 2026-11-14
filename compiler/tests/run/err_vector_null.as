package {
	public class err_vector_null {
		public function err_vector_null() {
			trace("before");
			var v:Vector.<int>;
			trace(v == null);
			trace(v.length);
			trace("after");
		}
	}
}
