package {
    // A sum of whole numbers held untyped (in an Array, an Object, a `*`): an int where an int holds it, a
    // Number past an int's range, and nothing lost of a sum that is nothing: -0 stays -0.
    public class whole_untyped {
        public function whole_untyped() {
            trace(sums(0, 5), sums(-3, 4), sums(2147483640, 12), sums(-2147483645, 6));
            trace(unsigned(4294967290, 8), unsigned(0, 3), mixed(-5, 3), mixed(7, 4000000000));
            trace(differences(3), differences(-2147483647), three(1000000000), three(-1000000000));
            trace(said(0, 0), said(-1, 1), said(2147483647, 1), said(-2147483648, -1), said(5, -5));
            trace(1 / nothing(0), 1 / nothingBack(0), 1 / plain(0), 1 / fromConstant(), 1 / minus(0));
            var held:Object = { at: 0 };
            for (var i:int = -2; i < 3; i++) { held["k" + (i + 2)] = i + i; }
            trace(held.k0, held.k1, held.k2, held.k3, held.k4, held.k2 is int, held.k4 is int);
        }

        private function sums(from:int, count:int):String {
            var out:Array = [];
            for (var i:int = 0; i < count; i++) { out.push(from + i); }
            return out.join(",") + "/" + kinds(out);
        }

        private function unsigned(from:uint, count:uint):String {
            var out:Array = [];
            for (var i:uint = 0; i < count; i++) { out.push(from + i); }
            return out.join(",") + "/" + kinds(out);
        }

        private function mixed(a:int, b:uint):String {
            var out:Array = [a + b, b + a, a - b, b - a, a + a + b];
            return out.join(",") + "/" + kinds(out);
        }

        private function differences(a:int):String {
            var out:Array = [a - 1, a - 2, 1 - a, a - a, a - 3 + 3];
            return out.join(",") + "/" + kinds(out);
        }

        private function three(a:int):String {
            var out:Array = [a + a + a, a + a, a + a + a - a - a];
            return out.join(",") + "/" + kinds(out);
        }

        private function said(a:int, b:int):String {
            var sum:* = a + b;
            var less:* = a - b;
            return sum + ":" + (sum is int) + ":" + less + ":" + (less is int);
        }

        private function nothing(a:int):* { return -0.0 - a; }
        private function nothingBack(a:int):* { return a - 0.0; }
        private function plain(a:int):* { return a + a; }
        private function fromConstant():* { var zero:Number = -0.0; var held:* = zero + -0.0; return held; }
        private function minus(a:int):* { var b:int = 0; return -0.0 - a - b; }

        private function kinds(values:Array):String {
            var out:String = "";
            for each (var value:* in values) { out += value is int ? "i" : value is uint ? "u" : "n"; }
            return out;
        }
    }
}
