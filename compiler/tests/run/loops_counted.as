package {
    // Loops counted by a whole number against a bound, in every shape native code writes one: what each
    // gives must not depend on how often the time a call has run is looked at while it goes round.
    public class loops_counted {
        private static const LIMIT:int = 10000;
        private var edge:int = 9000;
        private var cells:Vector.<int> = new Vector.<int>(12000);
        private var numbers:Vector.<Number> = new Vector.<Number>(9001);

        public function loops_counted() {
            trace(up(0, 10000, 1), up(5, 10000, 3), up(-7000, 7000, 7), up(3, 3, 1), up(9, 3, 1), up(0, 5, 100000));
            trace(upInclusive(0, 4096), upInclusive(1, 8192), upInclusive(-4096, 4096), upInclusive(4, 3));
            trace(down(10000, 0, 1), down(10000, -10000, 9), down(3, 3, 1), down(-5, 5, 1), downInclusive(8192, 0), downInclusive(0, 1));
            trace(unsigned(0, 9000, 1), unsigned(4294960000, 4294967295, 5), byConstant(), byField(), byStatic());
            trace(atTheEdge(2147483640, 2147483647, 1), atTheEdge(2147483000, 2147483646, 3), wraps(2147483640, 9), wraps(2147483646, 5000));
            trace(lowEdge(-2147483640, 1), lowEdge(-2147483000, 7), unsignedEdge(4294967290, 9), unsignedDown(9000, 7));
            trace(noStep(5, 10, 0, 6), noStep(5, 10, -1, 6), noStepDown(5, 0, 0, 4), noStepDown(5, 0, -2, 4));
            trace(nested(120), nestedDown(90), triangle(200), movingBound(50), counterWritten(40), skips(100), twoWays(9000), leaves(100000));
            trace(fill(12000), total(12000), strided(12000, 7), partial(100, 9000), backwards(11999), numbersTotal());
            trace(pastTheEnd(12001), belowTheStart(-3), throughCalls(5000), inTry(5000), twoTests(5000, 4000));
        }

        private function up(from:int, to:int, step:int):Number {
            var sum:Number = 0;
            for (var i:int = from; i < to; i += step) { sum += i; }
            return sum;
        }

        private function upInclusive(from:int, to:int):Number {
            var sum:Number = 0;
            for (var i:int = from; i <= to; i++) { sum += i * 2; }
            return sum;
        }

        private function down(from:int, to:int, step:int):Number {
            var sum:Number = 0;
            for (var i:int = from; i > to; i -= step) { sum += i; }
            return sum;
        }

        private function downInclusive(from:int, to:int):Number {
            var sum:Number = 0;
            for (var i:int = from; i >= to; i--) { sum += i; }
            return sum;
        }

        private function unsigned(from:uint, to:uint, step:uint):Number {
            var sum:Number = 0;
            for (var i:uint = from; i < to; i += step) { sum += i; }
            return sum;
        }

        private function byConstant():Number {
            var sum:Number = 0;
            for (var i:int = 0; i < 9999; i++) { sum += i & 15; }
            return sum;
        }

        private function byField():Number {
            var sum:Number = 0;
            for (var i:int = 0; i < edge; i += 2) { sum += i; }
            return sum;
        }

        private function byStatic():Number {
            var sum:Number = 0;
            for (var i:int = 1; i <= LIMIT; i++) { sum += i; }
            return sum;
        }

        // Up to the greatest int, which a step of 3 passes: the counter goes on from the least.
        private function atTheEdge(from:int, to:int, step:int):Number {
            var sum:Number = 0;
            var count:int = 0;
            for (var i:int = from; i < to; i += step) {
                sum += i % 1000;
                if (++count >= 300) { break; }
            }
            return sum + count;
        }

        // The counter passes the greatest int and goes on from the least: the loop ends by its count alone.
        private function wraps(from:int, rounds:int):Number {
            var sum:Number = 0;
            var count:int = 0;
            for (var i:int = from; i <= 2147483647; i++) {
                sum += i % 1000;
                if (++count >= rounds) { break; }
            }
            return sum + count;
        }

        // Down to the least int, which a step of 7 passes: the counter goes on from the greatest.
        private function lowEdge(from:int, step:int):Number {
            var sum:Number = 0;
            var count:int = 0;
            for (var i:int = from; i > -2147483648; i -= step) {
                sum += i % 1000;
                if (++count >= 200) { break; }
            }
            return sum + count;
        }

        private function unsignedEdge(from:uint, rounds:int):Number {
            var sum:Number = 0;
            var count:int = 0;
            for (var i:uint = from; i <= 4294967295; i++) {
                sum += i % 100;
                if (++count >= rounds) { break; }
            }
            return sum + count;
        }

        private function unsignedDown(from:uint, step:uint):Number {
            var sum:Number = 0;
            for (var i:uint = from; i > 6; i -= step) { sum += i; }
            return sum;
        }

        // A step that is nothing, or that goes the other way: the loop ends by its count alone.
        private function noStep(from:int, to:int, step:int, rounds:int):Number {
            var sum:Number = 0;
            var count:int = 0;
            for (var i:int = from; i < to; i += step) {
                sum += i;
                if (++count >= rounds) { break; }
            }
            return sum;
        }

        private function noStepDown(from:int, to:int, step:int, rounds:int):Number {
            var sum:Number = 0;
            var count:int = 0;
            for (var i:int = from; i > to; i -= step) {
                sum += i;
                if (++count >= rounds) { break; }
            }
            return sum;
        }

        // An inner loop whose bound is the outer loop's counter: another bound each time it is entered.
        private function nested(size:int):Number {
            var sum:Number = 0;
            for (var i:int = 0; i < size; i++) {
                for (var j:int = 0; j < i; j++) { sum += j; }
                for (var k:int = size; k > i; k--) { sum -= 1; }
            }
            return sum;
        }

        private function nestedDown(size:int):Number {
            var sum:Number = 0;
            for (var i:int = size; i > 0; i--) {
                for (var j:int = i; j <= size; j += 2) { sum += i * j; }
            }
            return sum;
        }

        private function triangle(size:int):Number {
            var sum:Number = 0;
            for (var i:int = 0; i < size; i++) {
                for (var j:int = size - i; j < size + i; j++) { sum += 1; }
            }
            return sum;
        }

        // A bound that the loop moves, and a counter that it writes besides the step.
        private function movingBound(size:int):Number {
            var sum:Number = 0;
            var end:int = size;
            for (var i:int = 0; i < end; i++) {
                sum += i;
                if (i % 10 == 0 && end < 5000) { end += 97; }
            }
            return sum;
        }

        private function counterWritten(size:int):Number {
            var sum:Number = 0;
            var turns:int = 0;
            for (var i:int = 0; i < size; i++) {
                sum += i;
                if (i == size - 1 && turns < 300) { turns++; i = turns % 7; }
            }
            return sum;
        }

        private function skips(size:int):Number {
            var sum:Number = 0;
            var i:int = 0;
            while (i < size * 100) {
                i += 3;
                if (i % 2 == 0) { continue; }
                sum += i;
            }
            return sum;
        }

        // A second way round, that goes back to the test without the step the first way makes.
        private function twoWays(size:int):Number {
            var sum:Number = 0;
            var i:int = 0;
            while (i < size) {
                i++;
                if ((i & 1) == 1) { continue; }
                sum += i;
            }
            var j:int = 0;
            while (j < size) {
                if (sum > 0 && (j & 7) == 3) { j += 2; continue; }
                j++;
                sum -= 1;
            }
            return sum;
        }

        private function leaves(size:int):Number {
            var sum:Number = 0;
            for (var i:int = 0; i < size; i++) {
                sum += i;
                if (sum > 40000000) { break; }
            }
            return sum;
        }

        private function fill(count:int):Number {
            for (var i:int = 0; i < cells.length; i++) { cells[i] = i * 3 - count; }
            for (var j:int = 0; j < numbers.length; j++) { numbers[j] = j * 0.5; }
            return cells[cells.length - 1];
        }

        private function total(count:int):Number {
            var sum:Number = 0;
            for (var i:uint = 0; i < cells.length; i++) { sum += cells[i]; }
            for (var j:int = 0; j < count; j++) { sum += cells[j] & 3; }
            return sum;
        }

        private function strided(count:int, stride:int):Number {
            var sum:Number = 0;
            for (var i:int = stride; i < count; i += stride) { cells[i] = cells[i - stride] + 1; sum += cells[i]; }
            return sum;
        }

        private function partial(from:int, to:int):Number {
            var sum:Number = 0;
            for (var i:int = from; i <= to; i++) { numbers[i] = numbers[i] + numbers[i - 1]; sum += numbers[i]; }
            return sum;
        }

        private function backwards(from:int):Number {
            var sum:Number = 0;
            for (var i:int = from; i >= 0; i -= 5) { sum += cells[i]; cells[i] = 1; }
            return sum;
        }

        private function numbersTotal():Number {
            var sum:Number = 0;
            for (var i:int = 0; i < numbers.length; i++) { sum += numbers[i]; }
            return Math.round(sum);
        }

        // An index that is not one of the vector's is the vector's RangeError, wherever the loop is.
        private function pastTheEnd(count:int):String {
            var sum:Number = 0;
            try {
                for (var i:int = 0; i < count; i++) { sum += cells[i]; }
            } catch (error:RangeError) {
                return "out of range after " + sum;
            }
            return "no error";
        }

        private function belowTheStart(from:int):String {
            var sum:Number = 0;
            try {
                for (var i:int = from; i < cells.length; i++) { sum += cells[i]; }
            } catch (error:RangeError) {
                return "out of range at once";
            }
            return "no error";
        }

        private function throughCalls(count:int):Number {
            var sum:Number = 0;
            for (var i:int = 0; i < count; i++) { sum += half(i) + cells[i % 12000]; }
            return sum;
        }

        private function half(value:int):Number { return value >> 1; }

        private function inTry(count:int):Number {
            var sum:Number = 0;
            for (var i:int = 0; i < count; i++) {
                try {
                    if (i % 1000 == 999) { throw new Error("x"); }
                    sum += i;
                } catch (error:Error) {
                    sum -= 1;
                }
            }
            return sum;
        }

        private function twoTests(count:int, other:int):Number {
            var sum:Number = 0;
            for (var i:int = 0; i < count && i < other; i++) { sum += i; }
            return sum;
        }
    }
}
