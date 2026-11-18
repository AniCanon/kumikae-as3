package {
    // Calls without a loop: a recursion that would take hours is stopped as a loop is. Where it is when the
    // time is up is not the same twice, so only the error's first line is held.
    public class calls {
        public function calls() {
            trace("before");
            try {
                trace(fib(70));
            } catch (error:Error) {
                trace("caught");
            } finally {
                trace("finally");
            }
            trace("after");
        }

        private function fib(n:int):Number {
            return n < 2 ? n : fib(n - 1) + fib(n - 2);
        }
    }
}
