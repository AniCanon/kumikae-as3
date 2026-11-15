package {
	public class err_vector_concat_null {
		public function err_vector_concat_null() {
			trace("before");
			var v:Vector.<int> = new <int>[1];
			var none:Vector.<int>;
			trace(v.concat(none));
			trace("after");
		}
	}
}
