package {
    // A loop that counts down by nothing never ends.
    public class counted_down {
        public function counted_down() {
            trace(run(3, 1));
            trace(run(3, 0));
            trace("after");
        }

        private function run(size:int, step:int):Number {
            var sum:Number = 0;
            for (var i:int = size; i > 0; i -= step) { sum += i; }
            return sum;
        }
    }
}
