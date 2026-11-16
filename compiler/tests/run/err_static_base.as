package {
	// A class is first used inside a method, and its base's static fields fail to be set: the error leaves
	// through both classes' setting up, and the next use fails again.
	public class err_static_base {
		public function err_static_base() {
			try {
				helper();
			} catch (e:Error) {
				trace("first", e.message);
			}
			helper();
		}

		private function helper():void {
			trace(Leaf.b);
		}
	}
}

class Root {
	public static var a:int = fail();

	private static function fail():int {
		throw new Error("no Root");
	}
}

class Leaf extends Root {
	public static var b:int = 2;
}
