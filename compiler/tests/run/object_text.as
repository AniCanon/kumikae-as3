package {
	public class object_text {
		public function object_text() {
			var plain:Plain = new Plain();
			var named:Named = new Named();
			var base:Plain = new Child();
			trace(plain.toString(), named.toString(), base.toString(), this.toString(), toString());
			trace(new Grand().toString(), String(new Grand()), "" + new Grand());
			var e:Error = new Error("boom");
			trace(e.toString(), new Error().toString(), new RangeError("far").toString(), new Mine().toString(), String(new Mine()), new Mine());
			// An Error reads as its name, which a program may change, and its message.
			e.name = "Custom";
			trace(e.toString(), e, "" + e);
			e.message = "";
			trace(e.toString());
			var o:Object = named;
			var x:* = 5;
			var t:Object = "text";
			trace(o.toString(), x.toString(), t.toString(), new Supered().toString(), new SuperError().toString());
			var count:int = 0;
			var missing:Plain = null;
			try { missing.toString(); } catch (caught:TypeError) { trace(caught.errorID); }
			var nothing:Object = null;
			try { nothing.toString(); } catch (caught:TypeError) { trace(caught.errorID); }
			var unset:*;
			try { unset.toString(); } catch (caught:TypeError) { trace(caught); }
			var words:String = null;
			try { words.charAt(count++); } catch (caught:TypeError) { trace(caught.errorID, count); }
			try { words.toString(); } catch (caught:TypeError) { trace(caught.errorID); }
		}
	}
}

class Plain {}
class Named { public function toString():String { return "named"; } }
class Child extends Plain { public function toString():String { return "child"; } }
class Grand extends Child {}
class Mine extends Error { public function Mine() { super("mine"); } }
class Supered { public function toString():String { return "<" + super.toString() + ">"; } }
class SuperError extends Error {
	public function SuperError() { super("deep"); }
	public function toString():String { return "[" + super.toString() + "]"; }
}
