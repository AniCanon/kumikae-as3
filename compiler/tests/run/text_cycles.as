package {
	// An Array or a Vector that holds itself, or nests very deep, put into words. avmplus's Array.join and
	// Vector's join are AS3 code with no guard against a cycle (core/Array.as, core/VectorImpl.as): each
	// element's text calls join again until the stack check stops it with Error #1023, which can be caught.
	// Here the same error comes from a limit on how deep the text goes.
	public class text_cycles {
		public function text_cycles() {
			var a:Array = [1, 2];
			a[1] = a;
			attempt("trace", function():void { trace(a); });
			attempt("String", function():void { trace(String(a)); });
			var b:Array = [3];
			b.push(b);
			attempt("join", function():void { trace(b.join()); });
			var p:Array = [];
			var q:Array = [p];
			p.push(q);
			attempt("plus", function():void { trace("x" + p); });
			var v:Vector.<Object> = new Vector.<Object>();
			v.push(1);
			v.push(v);
			attempt("vector", function():void { trace(v.join("-")); });
			var deep:Array = [];
			for (var i:int = 0; i < 40000; i++) {
				deep = [deep];
			}
			attempt("deep", function():void { trace(deep.toString().length); });
			var o:Object = {};
			o.list = [o];
			o.toString = function():String { return "o:" + o.list; };
			attempt("object", function():void { trace(String(o)); });
			var shallow:Array = [];
			for (var j:int = 0; j < 100; j++) {
				shallow = [shallow, j];
			}
			trace(shallow.toString().length);
			a[1] = 0;
			b.pop();
			p.pop();
			v.pop();
			o.list = null;
			trace("done");
		}

		private function attempt(name:String, f:Function):void {
			try {
				f();
			} catch (e:Error) {
				trace(name + ": " + e.message);
			}
		}
	}
}
