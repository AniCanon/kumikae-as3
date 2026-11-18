package {
    // A loop without end is stopped by Error #1502, which no catch takes and no finally runs after.
    public class loop {
        public function loop() {
            trace("before");
            try {
                spin();
            } catch (error:Error) {
                trace("caught");
            } finally {
                trace("finally");
            }
            trace("after");
        }

        private function spin():void {
            var n:int = 0;
            try { while (true) { n++; } } catch (error:*) { trace("caught inside"); } finally { trace("finally inside"); }
        }
    }
}
