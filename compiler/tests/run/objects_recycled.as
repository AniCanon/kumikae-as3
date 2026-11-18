package {
    // Objects that are numbers and nothing more, made and let go of in a loop: native code makes the next
    // where the last one was. Each is its own, whatever was there before: of another class of the same
    // size, made with fewer arguments, made by a constructor that calls its base's, one that takes text, or
    // one that holds text.
    // Run with what objects are made with remembered too (run_native.py), it gives the same and leaks nothing.
    public class objects_recycled {
        private var kept:Vector.<Pair> = new Vector.<Pair>();

        public function objects_recycled() {
            trace(pairs(5000), mixed(5000), defaults(3000), derived(3000), named(2000), keptSome(4000), labelled(3000));
            trace(kept.length, kept[0].a, kept[0].b, kept[99].a, kept[99].b);
        }

        private function pairs(count:int):Number {
            var sum:Number = 0;
            for (var i:int = 0; i < count; i++) {
                var pair:Pair = new Pair(i, 2);
                sum += pair.a * pair.b;
            }
            return sum;
        }

        // Three classes whose objects are of one size, turn and turn about.
        private function mixed(count:int):Number {
            var sum:Number = 0;
            for (var i:int = 0; i < count; i++) {
                var pair:Pair = new Pair(i, 1);
                var other:Other = new Other(i + 0.5, -1);
                var flag:Flagged = new Flagged(i, (i & 1) == 0);
                sum += pair.a + other.x + other.y + (flag.on ? flag.n : 0);
                pair = null;
                other = null;
            }
            return sum;
        }

        private function defaults(count:int):Number {
            var sum:Number = 0;
            for (var i:int = 0; i < count; i++) {
                var most:Pair = new Pair(i);
                var none:Pair = new Pair();
                sum += most.a + most.b + none.a + none.b;
            }
            return sum;
        }

        private function derived(count:int):Number {
            var sum:Number = 0;
            for (var i:int = 0; i < count; i++) {
                var triple:Triple = new Triple(i, 3, 4);
                var pair:Pair = new Pair(5, i);
                sum += triple.a + triple.b + triple.c + pair.b;
            }
            return sum;
        }

        private function named(count:int):Number {
            var sum:Number = 0;
            for (var i:int = 0; i < count; i++) {
                var tag:Tagged = new Tagged("t" + (i % 7), i);
                var pair:Pair = new Pair(i, i);
                sum += tag.name.length + tag.n + pair.b;
            }
            return sum;
        }

        // A class that holds text, which the heap frees: made with numbers all the same.
        private function labelled(count:int):Number {
            var sum:Number = 0;
            for (var i:int = 0; i < count; i++) {
                var one:Labelled = new Labelled(i, 0.5);
                sum += one.n * one.scale + one.label.length;
            }
            return sum;
        }

        // Some stay, in a vector: each keeps what it was made with while those around it come and go.
        private function keptSome(count:int):Number {
            var sum:Number = 0;
            for (var i:int = 0; i < count; i++) {
                var pair:Pair = new Pair(i, i * 2);
                if (i % 40 == 0) { kept.push(pair); }
                sum += pair.b;
            }
            for (var j:int = 0; j < kept.length; j++) { sum += kept[j].a - kept[j].b; }
            return sum;
        }
    }
}

class Pair {
    public var a:Number;
    public var b:Number;
    public function Pair(a:Number = 7, b:Number = 9) { this.a = a; this.b = b; }
}

class Other {
    public var x:Number;
    public var y:Number;
    public function Other(x:Number, y:Number) { this.x = x * 2; this.y = y; }
}

class Flagged {
    public var n:int;
    public var on:Boolean;
    public function Flagged(n:int, on:Boolean) { this.n = n; this.on = on; }
}

class Triple extends Pair {
    public var c:Number;
    public function Triple(a:Number, b:Number, c:Number) { super(a, b); this.c = c; }
}

class Labelled {
    public var n:Number;
    public var scale:Number;
    public var label:String = "label";
    public function Labelled(n:Number, scale:Number) { this.n = n; this.scale = scale; }
}

class Tagged {
    public var name:String;
    public var n:Number;
    public function Tagged(name:String, n:Number) { this.name = name; this.n = n; }
}
