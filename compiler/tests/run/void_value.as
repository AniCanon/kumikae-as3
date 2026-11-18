package {
	// A call that gives nothing, used as a value: undefined.
	public class void_value {
		public function void_value() {
			trace(nothing());
			var held:* = nothing();
			trace(held, held === undefined, typeof nothing());
			var text:String = "got " + nothing();
			trace(text);
			still();
			trace([nothing(), 1].length, [nothing()][0] === undefined);
			var count:int = 0;
			trace(void 0, void count++, count, typeof void 0, (void 0) === undefined);
			void count++;
			var cleared:* = void count;
			trace(count, cleared, "is " + void 0);
		}

		private function nothing():void {}
		private static function still():void {}
	}
}
