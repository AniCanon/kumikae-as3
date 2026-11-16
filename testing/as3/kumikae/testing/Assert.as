package kumikae.testing {
    public class Assert {
        public static function assertTrue(value:Boolean, message:String = "Expected true"):void {
            if (!value) {
                throw new Error(message);
            }
        }

        public static function assertEquals(expected:*, actual:*, message:String = "Values differ"):void {
            if (expected !== actual) {
                throw new Error(message + ": expected " + expected + ", got " + actual);
            }
        }

        public static function fail(message:String):void {
            throw new Error(message);
        }
    }
}
