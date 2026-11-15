package {
	public class err_vector_read {
		public function err_vector_read() {
			trace("before");
			var v:Vector.<int> = new <int>[1, 2, 3];
			trace(v[3]);
			trace("after");
		}
	}
}
