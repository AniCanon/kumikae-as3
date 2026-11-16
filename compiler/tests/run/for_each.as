package {
	public class for_each {
		public function for_each() {
			var words:Vector.<String> = new <String>["a", "b", "c"];
			var joined:String = "";
			for each (var word:String in words) {
				joined += word;
			}
			trace(joined);
			// Over an empty vector the body never runs.
			var none:Vector.<Number> = new Vector.<Number>();
			for each (var n:Number in none) {
				trace("never");
			}
			// A vector that grows while it is walked is walked to its end as it is then.
			var grows:Vector.<int> = new <int>[1, 2];
			var seen:int = 0;
			for each (var g:int in grows) {
				seen++;
				if (grows.length < 4) {
					grows.push(g * 10);
				}
			}
			trace(seen, grows);
			// break and continue.
			var odd:int = 0;
			for each (var v:int in new <int>[1, 2, 3, 4, 5]) {
				if (v == 4) {
					break;
				}
				if (v % 2 == 0) {
					continue;
				}
				odd += v;
			}
			trace(odd);
			// A local declared outside the loop holds the last element after it.
			var last:String;
			for each (last in words) {}
			trace(last);
		}
	}
}
