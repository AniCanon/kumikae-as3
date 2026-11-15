package {
	// What JSON.stringify and JSON.parse raise. A structure that holds itself is TypeError #1129, and a
	// replacer that is neither a function nor an Array is TypeError #1131 (ECMAScript passes over such a
	// replacer; Flash raises): both messages are avmplus's, from recollection, not checked against Flash.
	// More than 1,000 levels is Error #1023, the interpreter's ruling. An error thrown by a reviver, a
	// replacer, a toJSON() or a getter goes on its way out, and nothing made meanwhile is left alive.
	public class json_stringify_errors {
		public function json_stringify_errors() {
			var self:Object = {a: 1};
			self.me = self;
			attempt(function():* { return JSON.stringify(self); });
			var list:Array = [1];
			list.push([list]);
			attempt(function():* { return JSON.stringify(list); });
			var link:Link = new Link();
			link.next = {back: link};
			attempt(function():* { return JSON.stringify(link); });
			attempt(function():* { return JSON.stringify(1, "x"); });
			attempt(function():* { return JSON.stringify(1, 5); });
			attempt(function():* { return JSON.stringify({a: 1}, function(k:String, v:*):* { throw new Error("from the replacer"); }); });
			attempt(function():* { return JSON.stringify([new Thrower()]); });
			attempt(function():* { return JSON.stringify({g: new Getter()}); });
			attempt(function():* { return JSON.parse('{"a":[1]}', function(k:String, v:*):* { if (k == "0") { throw new RangeError("from the reviver"); } return v; }); });

			var deep:Array = [];
			for (var i:int = 1; i < 1000; i++) {
				deep = [deep];
			}
			trace(JSON.stringify(deep).length);
			attempt(function():* { return JSON.stringify([deep]); });
			attempt(function():* { return JSON.stringify({a: [deep]}, null, 1); });
			// A toJSON() that writes its own object again goes down without end, until AS3's calls run out.
			attempt(function():* { return JSON.stringify(new Again()); });
		}

		private function attempt(p_work:Function):void {
			try {
				trace("gave", p_work());
			} catch (e:Error) {
				trace(e);
			}
		}
	}
}

class Link {
	public var next:Object;
}

class Thrower {
	public function toJSON(p_key:String):* {
		throw new ArgumentError("from toJSON " + p_key);
	}
}

class Again {
	public function toJSON(p_key:String):* {
		return JSON.stringify(this);
	}
}

class Getter {
	public function get broken():int {
		throw new Error("from a getter");
	}
}
