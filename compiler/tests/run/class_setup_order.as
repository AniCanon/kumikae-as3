package {
	public class class_setup_order {
		public function class_setup_order() {
			Counter.add(note("argument"));
			var o:Object = null;
			trace(o is Later);
		}
		private function note(p_text:String):int {
			trace(p_text);
			return 1;
		}
	}
}

class Counter {
	public static var total:int = start();
	private static function start():int { trace("Counter set up"); return 0; }
	public static function add(p_value:int):void { total += p_value; trace("total", total); }
}

class Later {
	public static var ready:Boolean = start();
	private static function start():Boolean { trace("Later set up"); return true; }
}
