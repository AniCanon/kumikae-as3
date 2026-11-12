package {
	public class flow {
		private var log:String = "";
		private var count:int = 0;
		private static var calls:int = 0;

		public function flow() {
			// Strings.
			var s:String = "a" + 1 + 2;
			var t:String = 1 + 2 + "a";
			var none:String;
			trace(s, t, none, none + "!", "x" + none, "" + true + null);
			trace("abc".length, s.length, "héllo \"q\" \\ \x41\ttab".length, "tab\there");
			trace("a" < "b", "10" < "9", "abc" == "abc", "abc" != "abd", none == null, "" == none, "b" >= "a");
			s += "!" + 3;
			s += 4;
			trace(s);
			var e:String = "";
			trace(e ? "yes" : "no", s ? "yes" : "no", none ? "yes" : "no");

			// Loops, break and continue.
			var out:String = "";
			for (var i:int = 0; i < 10; i++) {
				if (i % 2 == 0) { continue; }
				if (i > 7) { break; }
				out += i;
			}
			trace(out, i);
			var n:int = 3;
			while (n > 0) { out += "w" + n; n--; }
			do { out += "d"; n++; } while (n < 2);
			trace(out);
			for (;;) { n++; if (n > 5) break; }
			var k:int = 0, total:int = 0;
			for (k = 0; k < 4; k++) { for (var m:int = 0; m < 4; m++) { if (m == 2) break; total += k * 10 + m; } }
			trace(n, k, total);
			// A variable declared in a loop is declared once: it keeps its value.
			for (i = 0; i < 3; i++) { var kept:int; kept += i + 1; }
			trace(kept);

			// Evaluation order: left to right, each operand once.
			trace(note("a", 1) + note("b", 2) * note("c", 3), log);
			log = "";
			trace(note("x", 0) && note("y", 1), note("p", 5) || note("q", 6), log);
			log = "";
			trace(yes("1") || yes("2"), no("3") && yes("4"), no("5") || yes("6"), !yes("7"), log);
			var a:int = 5;
			var r:int = a + (a = 10) + a;
			trace(r, a);
			a = 1;
			r = a++ + a++ * ++a;
			trace(r, a);
			a = 3;
			a += a++ + ++a;
			trace(a);
			count = 2;
			this.count += count++ * 10;
			trace(count);
			trace(next() + next() * next(), calls);
			trace(pick(next(), next(), next()), calls);
			var c:int = a > 10 ? note("big", 1) : note("small", 2);
			trace(c, log, a > 100 ? "huge" : a > 10 ? "big" : "small");
			trace((note("first", 1), note("second", 2)));
			trace(fact(10), fib(20), sum(100), early(5), early(-5));
		}

		private function note(name:String, value:int):int { log += name; return value; }
		private function yes(name:String):Boolean { log += name; return true; }
		private function no(name:String):Boolean { log += name; return false; }
		private static function next():int { return ++calls; }
		private function pick(a:int, b:int, c:int):String { return a + "-" + b + "-" + c; }
		private function fact(n:int):Number { return n <= 1 ? 1 : n * fact(n - 1); }
		private function fib(n:int):int { if (n < 2) return n; return fib(n - 1) + fib(n - 2); }
		private function sum(n:int):int { var s:int = 0; while (n > 0) { s += n; n--; } return s; }
		private function early(n:int):String { if (n < 0) { return "negative"; } return "ok"; }
	}
}
