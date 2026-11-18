package {
    // A loop over a vector's elements that never ends, in each of the forms native code writes one.
    public class vector_loop {
        public function vector_loop() {
            var numbers:Vector.<Number> = new <Number>[1, 2, 3, 4];
            trace(sum(numbers, 1));
            trace(sum(numbers, 0));
            trace("after");
        }

        private function sum(numbers:Vector.<Number>, step:int):Number {
            var total:Number = 0;
            for (var i:int = 0; i < numbers.length; i += step) { total += numbers[i]; numbers[i] = total; }
            return total;
        }
    }
}
