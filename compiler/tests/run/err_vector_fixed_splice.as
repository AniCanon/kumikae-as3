package {
	public class err_vector_fixed_splice {
		public function err_vector_fixed_splice() {
			trace("before");
			var v:Vector.<int> = new <int>[1, 2, 3];
			v.fixed = true;
			trace(v.splice(1, 1, 7), v);
			v.splice(0, 1);
			trace("after");
		}
	}
}
