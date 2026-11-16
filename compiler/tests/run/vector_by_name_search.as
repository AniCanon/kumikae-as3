package {
	// indexOf() and lastIndexOf() search by ===: a value of another type is no element of a typed vector,
	// and a call by name on an untyped value answers what the typed call answers, converting nothing.
	// toString() takes no argument, by name as when typed.
	public class vector_by_name_search {
		public function vector_by_name_search() {
			var ints:Vector.<int> = new <int>[1, 2, 1];
			var whole:Number = 2;
			var part:Number = 1.5;
			trace(ints.indexOf(1), ints.lastIndexOf(1), ints.indexOf(whole), ints.indexOf(part));
			var u:* = ints;
			trace(u.indexOf(1), u.lastIndexOf(1), u.indexOf(whole), u.indexOf(part));
			trace(u.indexOf("1"), u.indexOf(true), u.indexOf(null), u.indexOf(undefined), u.lastIndexOf("1"), u.indexOf(new M()));
			var texts:Vector.<String> = new <String>["a", null, "1"];
			var t:* = texts;
			trace(texts.indexOf("1"), texts.indexOf(null), t.indexOf("1"), t.indexOf(1), t.indexOf(null), t.indexOf(true));
			var things:Vector.<M> = new <M>[new M(), null];
			var o:* = things;
			var other:Object = new N();
			trace(o.indexOf(new N()), o.indexOf(other), o.indexOf("x"), o.indexOf(7), o.indexOf(things[0]), o.indexOf(null), o.lastIndexOf(new N()));
			var flags:* = new <Boolean>[false, true];
			trace(flags.indexOf(true), flags.indexOf(1), flags.indexOf("true"), flags.indexOf(null));
			var numbers:* = new <Number>[1.5, 2];
			trace(numbers.indexOf(2), numbers.indexOf(1.5), numbers.indexOf("2"), numbers.indexOf(true));
			var counts:* = new <uint>[1, 2];
			trace(counts.indexOf(2), counts.indexOf(-1), counts.indexOf(2.5), counts.indexOf("2"));
			var list:* = [1, "1", null, true];
			trace(list.indexOf("1"), list.indexOf(1), list.indexOf(true), list.indexOf(null), list.indexOf(undefined), list.lastIndexOf("1"));
			trace(u.toString(), list.toString(), u.join("x"), list.join());
			for each (var held:* in [list, u, t]) {
				try {
					trace(held.toString("x"));
				} catch (e:ArgumentError) {
					trace(e.errorID, e.message);
				}
			}
		}
	}
}
class M {}
class N {}
