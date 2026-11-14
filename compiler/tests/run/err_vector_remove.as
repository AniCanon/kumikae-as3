package {
	public class err_vector_remove {
		public function err_vector_remove() {
			trace("before");
			var v:Vector.<int> = new <int>[1, 2];
			trace(v.removeAt(-2), v);
			v.removeAt(1);
			trace("after");
		}
	}
}
