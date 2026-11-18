package {
    // A counter held to at most the greatest int passes it and goes on from the least: the loop never ends.
    public class counted_wrap {
        public function counted_wrap() {
            trace(run(2147483003));
            trace(run(2147483647));
            trace("after");
        }

        private function run(end:int):Number {
            var sum:Number = 0;
            for (var i:int = 2147483000; i <= end; i++) { sum += i & 1; }
            return sum;
        }
    }
}
