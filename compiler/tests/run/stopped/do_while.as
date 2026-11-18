package {
    // A loop closed by its own test, in a function that nothing else could fail in.
    public class do_while {
        public function do_while() {
            trace(count(3));
            trace(count(-1));
            trace("after");
        }

        private function count(to:int):int {
            var n:int = 0;
            do { n++; } while (n != to || to < 0);
            return n;
        }
    }
}
