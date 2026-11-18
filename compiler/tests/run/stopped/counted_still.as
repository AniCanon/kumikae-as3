package {
    // A counted loop whose step is nothing never ends.
    public class counted_still {
        public function counted_still() {
            trace(run(3, 1));
            trace(run(3, 0));
            trace("after");
        }

        private function run(size:int, step:int):Number {
            var sum:Number = 0;
            for (var i:int = 0; i < size; i += step) { sum += i + 1; }
            return sum;
        }
    }
}
