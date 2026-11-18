package {
	import flash.utils.Dictionary;

	// A class of the library called as a function, as avmplus has each: Error(x) and its subclasses make an
	// error, RegExp(x) gives x itself or makes one, Date(x) gives the time now as text, and Dictionary(x)
	// is a cast.
	public class class_calls {
		public function class_calls() {
			var failed:Error = Error("boom");
			trace(failed, failed.message, failed.errorID, failed is Error);
			var ranged:Error = RangeError("far", 7);
			trace(ranged, ranged is RangeError, ranged.errorID, Error().message == "");
			var held:* = 5;
			trace(Error(held).message, TypeError(null).message, TypeError(undefined).message);
			try {
				throw ArgumentError("thrown without new");
			} catch (e:ArgumentError) {
				trace(e.message);
			}

			var letters:RegExp = /a+/g;
			trace(RegExp(letters) === letters, RegExp(letters, undefined) === letters, new RegExp(letters) === letters);
			try {
				trace(RegExp(letters, "i"));
			} catch (flags:TypeError) {
				trace(flags.errorID);
			}
			var made:RegExp = RegExp("b+", "g");
			trace(made.source, made.global, "abbbc".replace(RegExp("b+"), "-"), RegExp().source);
			var untyped:* = letters;
			trace(RegExp(untyped) === letters, RegExp(held).source);

			var now:String = Date();
			trace(now == new Date().toString(), Date(0) == now, Date(2001, 1, 1) == now, typeof Date());
			var calls:int = 0;
			Date(calls++, calls++);
			trace(calls);

			var table:Dictionary = new Dictionary();
			var object:Object = table;
			trace(Dictionary(object) === table, Dictionary(null), Dictionary(undefined));
			try {
				trace(Dictionary(held));
			} catch (wrong:TypeError) {
				trace(wrong.errorID, wrong.message);
			}
			try {
				trace(Dictionary({}));
			} catch (plain:TypeError) {
				trace(plain.errorID);
			}
			trace(Mine(new Mine("own")).message);
			try {
				trace(Mine("no cast"));
			} catch (own:TypeError) {
				trace(own.errorID);
			}
		}
	}
}

class Mine extends Error {
	public function Mine(message:String) {
		super(message);
	}
}
