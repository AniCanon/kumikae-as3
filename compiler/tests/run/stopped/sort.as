package {
    // The loop is in a function the library calls: the error crosses the library, and still no catch takes it.
    public class sort {
        public function sort() {
            var list:Array = [3, 1, 2];
            trace("before");
            try {
                list.sort(function(a:int, b:int):int { while (true) { a++; } return a - b; });
            } catch (error:Error) {
                trace("caught");
            } finally {
                trace("finally");
            }
            trace("after");
        }
    }
}
