package {
	import flash.utils.Dictionary;
	public class err_dictionary_weak {
		public function err_dictionary_weak() {
			var strong:Dictionary = new Dictionary(false);
			trace(strong);
			var weak:Boolean = true;
			var d:Dictionary = new Dictionary(weak);
			trace("not reached");
		}
	}
}
