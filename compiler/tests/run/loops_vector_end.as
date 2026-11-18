package {
    // A loop that reads or stores a vector's elements under its counter, where the vector is not as long as
    // the loop goes, or stops being so while the loop runs: each gives the RangeError, or the result, that
    // going round one element at a time gives. Native code asks nothing of an element whose index it has
    // held short of the vector's end, so what holds it there is what this is about.
    public class loops_vector_end {
        public static var shared:Vector.<int>;
        private var cells:Vector.<int>;
        private var ratios:Vector.<Number>;
        private var things:Vector.<Item>;
        private var shortener:Shortener;

        public function loops_vector_end() {
            attempt("readPast", readPast);
            attempt("readPastLong", readPastLong);
            attempt("readPastUnsigned", readPastUnsigned);
            attempt("readPastField", readPastField);
            attempt("readBelow", readBelow);
            attempt("storePast", storePast);
            attempt("storeAtEnd", storeAtEnd);
            attempt("fixedRead", fixedRead);
            attempt("fixedStore", fixedStore);
            attempt("bodyPops", bodyPops);
            attempt("bodyPopsField", bodyPopsField);
            attempt("bodyEmpties", bodyEmpties);
            attempt("bodyShortens", bodyShortens);
            attempt("bodySplices", bodySplices);
            attempt("bodyReplaces", bodyReplaces);
            attempt("bodyReplacesField", bodyReplacesField);
            attempt("calleePops", calleePops);
            attempt("calleeEmpties", calleeEmpties);
            attempt("calleeSplices", calleeSplices);
            attempt("calleeReplaces", calleeReplaces);
            attempt("calleeOfAnother", calleeOfAnother);
            attempt("virtualPops", virtualPops);
            attempt("closurePops", closurePops);
            attempt("closureReplaces", closureReplaces);
            attempt("staticEmptied", staticEmptied);
            attempt("sorterPops", sorterPops);
            attempt("twoLengths", twoLengths);
            attempt("twoLengthsStore", twoLengthsStore);
            attempt("twoLengthsGrow", twoLengthsGrow);
            attempt("oneAhead", oneAhead);
            attempt("oneBehind", oneBehind);
            attempt("twice", twice);
            attempt("afterTheStep", afterTheStep);
            attempt("down", down);
            attempt("downFromEnd", downFromEnd);
            attempt("downBelow", downBelow);
            attempt("byThree", byThree);
            attempt("byThreeStore", byThreeStore);
            attempt("byFar", byFar);
            attempt("inner", inner);
            attempt("innerShorter", innerShorter);
            attempt("ratiosPast", ratiosPast);
            attempt("ratiosStorePast", ratiosStorePast);
            attempt("ratiosPopped", ratiosPopped);
            attempt("thingsPast", thingsPast);
            attempt("thingsPopped", thingsPopped);
            attempt("thingsCalleePops", thingsCalleePops);
            attempt("thingsReplaced", thingsReplaced);
            attempt("flagsPast", flagsPast);
            attempt("countsPast", countsPast);
        }

        private function attempt(name:String, run:Function):void {
            try {
                trace(name, run());
            } catch (error:RangeError) {
                trace(name, "RangeError", error.errorID, error.message);
            }
        }

        private function ints(length:int, fixed:Boolean = false):Vector.<int> {
            var made:Vector.<int> = new Vector.<int>(length, fixed);
            for (var i:int = 0; i < length; i++) { made[i] = i + 1; }
            return made;
        }

        private function numbers(length:int):Vector.<Number> {
            var made:Vector.<Number> = new Vector.<Number>(length);
            for (var i:int = 0; i < length; i++) { made[i] = i + 0.5; }
            return made;
        }

        private function items(length:int):Vector.<Item> {
            var made:Vector.<Item> = new Vector.<Item>();
            for (var i:int = 0; i < length; i++) { made.push(new Item(i + 1)); }
            return made;
        }

        private function sumTo(v:Vector.<int>, count:int):Number {
            var sum:Number = 0;
            for (var i:int = 0; i < count; i++) { sum += v[i]; }
            return sum;
        }

        private function readPast():Number { return sumTo(ints(10), 11); }

        // Longer than native code goes between two looks at the time.
        private function readPastLong():Number { return sumTo(ints(10000), 10003); }

        private function readPastUnsigned():Number {
            var v:Vector.<int> = ints(7);
            var sum:Number = 0;
            for (var i:uint = 2; i < 9; i++) { sum += v[i]; }
            return sum;
        }

        private function readPastField():Number {
            cells = ints(6);
            var sum:Number = 0;
            for (var i:int = 0; i <= cells.length; i++) { sum += cells[i]; }
            return sum;
        }

        private function readBelow():Number {
            var v:Vector.<int> = ints(5);
            var sum:Number = 0;
            for (var i:int = -2; i < v.length; i++) { sum += v[i]; }
            return sum;
        }

        private function storePast():Number {
            var v:Vector.<int> = ints(5);
            for (var i:int = 0; i < 9; i += 2) { v[i] = 7; }
            return v.length;
        }

        // A store at the vector's end makes it one longer, each round.
        private function storeAtEnd():String {
            var v:Vector.<int> = ints(5);
            for (var i:int = 0; i < 9; i++) { v[i] = i * 2; }
            return v.length + ": " + v;
        }

        private function fixedRead():Number { return sumTo(ints(5, true), 8); }

        private function fixedStore():Number {
            var v:Vector.<int> = ints(5, true);
            for (var i:int = 0; i < 6; i++) { v[i] = 3; }
            return v.length;
        }

        private function bodyPops():Number {
            var v:Vector.<int> = ints(10);
            var sum:Number = 0;
            for (var i:int = 0; i < 10; i++) {
                sum += v[i];
                if ((i & 1) == 0) { v.pop(); }
            }
            return sum;
        }

        private function bodyPopsField():Number {
            cells = ints(10);
            var sum:Number = 0;
            var count:int = cells.length;
            for (var i:int = 0; i < count; i++) {
                sum += cells[i];
                cells.pop();
            }
            return sum;
        }

        private function bodyEmpties():Number {
            var v:Vector.<int> = ints(10);
            var sum:Number = 0;
            for (var i:int = 0; i < 10; i++) {
                sum += v[i];
                if (i == 3) { v.length = 0; }
            }
            return sum;
        }

        private function bodyShortens():Number {
            var v:Vector.<int> = ints(10);
            var sum:Number = 0;
            for (var i:int = 0; i < 10; i++) {
                v[i] = v[i] + 1;
                v.length = v.length - 1;
                sum += i;
            }
            return sum;
        }

        private function bodySplices():Number {
            var v:Vector.<int> = ints(10);
            var sum:Number = 0;
            for (var i:int = 0; i < 10; i++) {
                sum += v[i];
                if (i == 4) { v.splice(1, 4); }
            }
            return sum;
        }

        private function bodyReplaces():Number {
            var v:Vector.<int> = ints(10);
            var sum:Number = 0;
            for (var i:int = 0; i < 10; i++) {
                sum += v[i];
                if (i == 5) { v = ints(7); }
            }
            return sum;
        }

        private function bodyReplacesField():Number {
            cells = ints(10);
            var sum:Number = 0;
            for (var i:int = 0; i < 10; i++) {
                sum += cells[i];
                if (i == 2) { cells = new Vector.<int>(4); }
            }
            return sum;
        }

        private function popOne():void { cells.pop(); }
        private function empty():void { cells.length = 0; }
        private function spliceTwo():void { cells.splice(0, 2); }
        private function replace():void { cells = ints(3); }

        private function calleePops():Number {
            cells = ints(10);
            var sum:Number = 0;
            for (var i:int = 0; i < 10; i++) {
                sum += cells[i];
                popOne();
            }
            return sum;
        }

        private function calleeEmpties():Number {
            cells = ints(10);
            var sum:Number = 0;
            var count:int = cells.length;
            for (var i:int = 0; i < count; i++) {
                cells[i] = 4;
                if (i == 6) { empty(); }
                sum += i;
            }
            return sum;
        }

        private function calleeSplices():Number {
            cells = ints(10);
            var v:Vector.<int> = cells;
            var sum:Number = 0;
            for (var i:int = 0; i < 10; i++) {
                sum += v[i];
                spliceTwo();
            }
            return sum;
        }

        private function calleeReplaces():Number {
            cells = ints(10);
            var sum:Number = 0;
            for (var i:int = 0; i < 10; i++) {
                sum += cells[i];
                if (i == 1) { replace(); }
            }
            return sum;
        }

        // The method of another object, which holds the same vector.
        private function calleeOfAnother():Number {
            var v:Vector.<int> = ints(10);
            shortener = new Shortener(v);
            var sum:Number = 0;
            for (var i:int = 0; i < 10; i++) {
                sum += v[i];
                shortener.once();
            }
            return sum;
        }

        private function virtualPops():Number {
            var walker:Walker = new Popping();
            return walker.walk(ints(10));
        }

        private function closurePops():Number {
            var v:Vector.<int> = ints(10);
            var pop:Function = function():void { v.pop(); v.pop(); };
            var sum:Number = 0;
            for (var i:int = 0; i < 10; i++) {
                sum += v[i];
                pop();
            }
            return sum;
        }

        private function closureReplaces():Number {
            var v:Vector.<int> = ints(10);
            var swap:Function = function():void { v = new Vector.<int>(2); };
            var sum:Number = 0;
            for (var i:int = 0; i < 10; i++) {
                sum += v[i];
                if (i == 0) { swap(); }
            }
            return sum;
        }

        private function staticEmptied():Number {
            shared = ints(10);
            var v:Vector.<int> = shared;
            var sum:Number = 0;
            for (var i:int = 0; i < 10; i++) {
                sum += v[i];
                if (i == 7) { Shortener.emptyShared(); }
            }
            return sum;
        }

        // A function the library calls back, from a loop that indexes the vector it shortens.
        private function sorterPops():Number {
            cells = ints(10);
            var pair:Array = [2, 1];
            var sum:Number = 0;
            for (var i:int = 0; i < 10; i++) {
                sum += cells[i];
                pair.sort(function(a:int, b:int):int { cells.pop(); return a - b; });
            }
            return sum;
        }

        private function twoLengths():Number {
            var a:Vector.<int> = ints(10);
            var b:Vector.<int> = ints(6);
            var sum:Number = 0;
            for (var i:int = 0; i < a.length; i++) { sum += a[i] * b[i]; }
            return sum;
        }

        private function twoLengthsStore():Number {
            var a:Vector.<int> = ints(4, true);
            var b:Vector.<int> = ints(9);
            for (var i:int = 0; i < b.length; i++) { a[i] = b[i] + 1; }
            return a.length;
        }

        // The shorter one grows as it is stored at its end, and the loop goes on to the longer one's.
        private function twoLengthsGrow():String {
            var a:Vector.<int> = ints(3);
            var b:Vector.<int> = ints(8);
            for (var i:int = 0; i < b.length; i++) { a[i] = b[i] * 10; }
            return a.length + ": " + a;
        }

        private function oneAhead():Number {
            var v:Vector.<int> = ints(10);
            var sum:Number = 0;
            for (var i:int = 0; i < v.length; i++) { sum += v[i] + v[i + 1]; }
            return sum;
        }

        private function oneBehind():Number {
            var v:Vector.<int> = ints(10);
            var sum:Number = 0;
            for (var i:int = 0; i < v.length; i++) { sum += v[i] + v[i - 1]; }
            return sum;
        }

        private function twice():Number {
            var v:Vector.<int> = ints(10);
            var sum:Number = 0;
            for (var i:int = 0; i < v.length; i++) { sum += v[i] + v[2 * i]; }
            return sum;
        }

        // The index is the counter after its step: one past what the test held.
        private function afterTheStep():Number {
            var v:Vector.<int> = ints(10);
            var sum:Number = 0;
            for (var i:int = 0; i < v.length;) {
                i++;
                sum += v[i];
            }
            return sum;
        }

        private function down():Number {
            var v:Vector.<int> = ints(10);
            var sum:Number = 0;
            for (var i:int = v.length - 1; i >= 0; i--) { sum += v[i]; }
            return sum;
        }

        private function downFromEnd():Number {
            var v:Vector.<int> = ints(10);
            var sum:Number = 0;
            for (var i:int = v.length; i >= 0; i--) { sum += v[i]; }
            return sum;
        }

        private function downBelow():Number {
            var v:Vector.<int> = ints(10);
            var sum:Number = 0;
            for (var i:int = v.length - 1; i >= -1; i--) { sum += v[i]; }
            return sum;
        }

        private function byThree():Number {
            var v:Vector.<int> = ints(10);
            var sum:Number = 0;
            for (var i:int = 0; i < 20; i += 3) { sum += v[i]; }
            return sum;
        }

        private function byThreeStore():String {
            var v:Vector.<int> = ints(10);
            for (var i:int = 1; i < 20; i += 3) { v[i] = 0; }
            return v.length + ": " + v;
        }

        private function byFar():Number {
            var v:Vector.<int> = ints(5000);
            var sum:Number = 0;
            for (var i:int = 0; i < 2000000000; i += 4999) { sum += v[i]; }
            return sum;
        }

        // The inner loop's vector is shorter than the outer's.
        private function inner():Number {
            var a:Vector.<int> = ints(6);
            var b:Vector.<int> = ints(4);
            var sum:Number = 0;
            for (var j:int = 0; j < a.length; j++) {
                for (var i:int = 0; i <= j; i++) { sum += a[j] * b[i]; }
            }
            return sum;
        }

        private function innerShorter():Number {
            var v:Vector.<int> = ints(9);
            var sum:Number = 0;
            for (var j:int = 0; j < 4; j++) {
                for (var i:int = 0; i < 7; i++) { sum += v[i]; }
                v.pop();
            }
            return sum;
        }

        private function ratiosPast():Number {
            ratios = numbers(8);
            var sum:Number = 0;
            for (var i:int = 0; i < 10; i++) { sum += ratios[i]; }
            return sum;
        }

        private function ratiosStorePast():Number {
            var v:Vector.<Number> = numbers(8);
            var w:Vector.<Number> = numbers(12);
            for (var i:int = 0; i < w.length; i += 2) { v[i] = w[i] * 2; }
            return v.length;
        }

        private function ratiosPopped():Number {
            ratios = numbers(8);
            var sum:Number = 0;
            for (var i:int = 0; i < 8; i++) {
                sum += ratios[i];
                ratios.pop();
            }
            return sum;
        }

        private function thingsPast():Number {
            var v:Vector.<Item> = items(6);
            var sum:Number = 0;
            for (var i:int = 0; i < 8; i++) {
                var each:Item = v[i];
                sum += each.value;
            }
            return sum;
        }

        private function thingsPopped():Number {
            things = items(6);
            var sum:Number = 0;
            for (var i:int = 0; i < 6; i++) {
                sum += things[i].value;
                things.pop();
            }
            return sum;
        }

        private function dropThing():void { things.length = things.length - 2; }

        private function thingsCalleePops():Number {
            things = items(9);
            var sum:Number = 0;
            var count:int = things.length;
            for (var i:int = 0; i < count; i++) {
                var each:Item = things[i];
                sum += each.value;
                dropThing();
            }
            return sum;
        }

        private function thingsReplaced():Number {
            things = items(9);
            var sum:Number = 0;
            for (var i:int = 0; i < 9; i++) {
                var each:Item = things[i];
                sum += each.doubled();
                if (i == 3) { things = items(2); }
            }
            return sum;
        }

        private function flagsPast():int {
            var flags:Vector.<Boolean> = new Vector.<Boolean>(5);
            var set:int = 0;
            for (var i:int = 0; i < 7; i += 2) { flags[i] = true; set++; }
            return set;
        }

        private function countsPast():Number {
            var counts:Vector.<uint> = new Vector.<uint>(6);
            var sum:Number = 0;
            for (var i:uint = 0; i < counts.length; i++) { counts[i] = i; sum += counts[i]; }
            for (var j:uint = 0; j < 9; j++) { sum += counts[j]; }
            return sum;
        }
    }
}

class Item {
    public var value:int;
    public function Item(value:int) { this.value = value; }
    public function doubled():int { return value * 2; }
}

class Shortener {
    private var held:Vector.<int>;
    public function Shortener(held:Vector.<int>) { this.held = held; }
    public function once():void { held.pop(); }
    public static function emptyShared():void { loops_vector_end.shared.length = 0; }
}

class Walker {
    protected var cells:Vector.<int>;
    protected function shorten():void { }
    public function walk(over:Vector.<int>):Number {
        cells = over;
        var sum:Number = 0;
        for (var i:int = 0; i < 10; i++) {
            sum += cells[i];
            shorten();
        }
        return sum;
    }
}

class Popping extends Walker {
    override protected function shorten():void { cells.pop(); cells.pop(); }
}
