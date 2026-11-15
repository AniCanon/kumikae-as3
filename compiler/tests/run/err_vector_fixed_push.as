package {
	public class err_vector_fixed_push {
		public function err_vector_fixed_push() {
			trace("before");
			var v:Vector.<int> = new Vector.<int>(2, true);
			trace(v.push(), v.indexOf(0));
			v.push(1);
			trace("after");
		}
	}
}
