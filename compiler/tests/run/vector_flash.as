package {
	public class vector_flash {
		public function vector_flash() {
			// From an empty vector, pop and shift give undefined made the element type, as in Flash.
			trace(new Vector.<Number>().pop(), new Vector.<int>().shift(), new Vector.<String>().pop(), new Vector.<Boolean>().pop());
			var fixed:Vector.<int> = new Vector.<int>(2, true);
			try { fixed.length = 2; } catch (e:RangeError) { trace(e.message); }
			trace(new <int>[1, 2].join(null), new <int>[1, 2].join());
		}
	}
}
