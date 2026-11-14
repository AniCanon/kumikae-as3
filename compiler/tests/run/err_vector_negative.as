package {
	public class err_vector_negative {
		public function err_vector_negative() {
			trace("before");
			var v:Vector.<int> = new <int>[1, 2, 3];
			var at:int = -1;
			v[at] = 4;
			trace("after");
		}
	}
}
