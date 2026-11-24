package {
	public class computed_key_index {
		private function remainder(a:int, b:int):Number { return a % b; }
		private function at(keys:Vector.<String>, index:Number):String {
			try { return keys[index]; }
			catch (error:RangeError) { return String(error.errorID); }
		}
		public function computed_key_index() {
			var names:Vector.<String> = new Vector.<String>();
			names.push("position", "modulate");
			trace(at(names, -0.0), at(names, 1), at(names, -1), at(names, 0.5), at(names, NaN), at(names, Infinity));
			trace(1 / remainder(-2147483648, -1), 1 / remainder(6, 3), remainder(-7, 3), remainder(7, -3), remainder(7, 0));
		}
	}
}
