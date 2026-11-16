package {
	public class vector_limit {
		public function vector_limit() {
			// One past the greatest length is refused the same way, however the vector is made to grow.
			try { var made:Vector.<int> = new Vector.<int>(1073741825); trace(made.length); } catch (e:Error) { trace(e.message); }
			var grown:Vector.<int> = new Vector.<int>();
			try { grown.length = 1073741825; } catch (e:Error) { trace(e.message); }
			var fixed:Vector.<int> = new Vector.<int>(2, true);
			try { fixed.length = 3; } catch (e:RangeError) { trace(e.message); }
			try { fixed.push(1); } catch (e:RangeError) { trace(e.message); }
			trace(grown.length, fixed.length);
		}
	}
}
