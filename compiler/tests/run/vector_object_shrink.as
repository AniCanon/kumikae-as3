// Regression coverage for Vector.<Object> shrink, Array.sortOn re-entrancy, and untyped Int64 vs String.
package {
	public class vector_object_shrink {
		public function vector_object_shrink() {
			// 1. Shrinking Vector.<Object> must keep surviving elements intact and not corrupt heap.
			var objects:Vector.<Object> = new Vector.<Object>(5);
			objects[0] = "a";
			objects[1] = 42;
			objects[2] = { k: 3 };
			objects.length = 2;
			trace(objects.length, objects[0], objects[1]);
			objects.length = 4;
			trace(objects.length, objects[2] === null, objects[3] === null);
			objects.length = 0;
			trace(objects.length);

			// 2. Array.sortOn with a re-entrant toString() that mutates names and options arrays.
			var items:Array = [{ a: 2, b: 1 }, { a: 1, b: 2 }, { a: 2, b: 0 }];
			var names:Array = [];
			var opts:Array = [];
			var mutator:Object = {
				toString: function():String {
					names.length = 0;
					opts.length = 0;
					return "a";
				}
			};
			names.push(mutator, "b");
			opts.push(Array.NUMERIC, Array.NUMERIC);
			items.sortOn(names, opts);
			trace(items[0].a + ":" + items[0].b, items[1].a + ":" + items[1].b, items[2].a + ":" + items[2].b);

			// 3. Untyped Int64 vs String above 2^53.
			var wide:Int64 = 9007199254740993;
			var u:* = wide;
			var s:* = "9007199254740993";
			var prev:* = "9007199254740992";
			trace(u == s, u == prev, u > prev, prev < u);
			var parsed:Int64 = s;
			trace(parsed == wide);

			// 4. String.split with an object delimiter whose toString throws.
			var boom:Object = {
				toString: function():String {
					throw new Error("delimiter failed");
				}
			};
			try {
				"a,b".split(boom);
				trace("no error");
			} catch (e:Error) {
				trace("split caught: " + e.message);
			}
		}
	}
}
