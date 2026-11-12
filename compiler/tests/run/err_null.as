package {
	public class err_null {
		public function err_null() {
			trace("before");
			var thing:Thing;
			go(thing);
			trace("after");
		}
		private function go(thing:Thing):void {
			thing.touch();
		}
	}
}
class Thing {
	public function touch():void {}
}
