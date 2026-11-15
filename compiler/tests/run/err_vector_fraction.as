package {
	public class err_vector_fraction {
		public function err_vector_fraction() {
			trace("before");
			var v:Vector.<int> = new <int>[1, 2, 3];
			var at:Number = 1.5;
			trace(v[at]);
			trace("after");
		}
	}
}
