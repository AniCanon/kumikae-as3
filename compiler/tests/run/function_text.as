package {
	// A function value as text: avmplus's Function.prototype.toString gives "function Function() {}" for every
	// function (core/Function.as: no source text is kept), a method closure included, which inherits it.
	public class function_text {
		public function function_text() {
			var expression:Function = function (a:int):int { return a; };
			var closure:Function = method;
			trace(expression);
			trace(closure);
			trace(String(expression), "" + closure, expression.toString());
			var u:* = closure;
			trace(u, String(u), u + "!");
			var o:Object = { f: expression };
			trace([expression, closure], o.f);
			var v:Vector.<Function> = new Vector.<Function>();
			v.push(closure);
			trace(v);
		}

		private function method():void {
		}
	}
}
