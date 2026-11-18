package {
	// A name that an Error or a Function does not have, read by name: undefined, as on any dynamic object
	// in AS3, where a Date and a RegExp already give it.
	public class absent_names {
		public function absent_names() {
			var failed:Error = new RangeError("far", 3);
			trace(failed["nope"], failed["message"], failed["errorID"], "nope" in failed, "message" in failed);
			var held:Object = failed;
			trace(held.nope === undefined, held.name, held.hasOwnProperty("nope"));
			var made:Function = function(a:int):int { return a; };
			var loose:Object = made;
			trace(loose.nope, loose.length, "nope" in loose, loose["call"] != null);
			for (var key:String in failed) {
				trace("never", key);
			}
			var own:Object = new Mine();
			try {
				trace(own.nope);
			} catch (e:ReferenceError) {
				trace(e.errorID);
			}
		}
	}
}

class Mine extends Error {
}
