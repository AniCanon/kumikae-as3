package app.anicanon.kumikae.testing {
    public class Assert {
        public static function assertTrue(value:Boolean, message:String = "Expected true"):void {
            if (!value) {
                throw new AssertionError(message, "true", "false", true);
            }
        }

        public static function assertFalse(value:Boolean, message:String = "Expected false"):void {
            if (value) {
                throw new AssertionError(message, "false", "true", true);
            }
        }

        public static function assertEquals(expected:*, actual:*, message:String = "Values differ"):void {
            if (expected !== actual) {
                throw new AssertionError(
                    message + ": expected " + expected + ", got " + actual,
                    String(expected),
                    String(actual),
                    true
                );
            }
        }

        public static function assertNotEquals(unexpected:*, actual:*, message:String = "Values should differ"):void {
            if (unexpected === actual) {
                throw new AssertionError(message + ": got " + actual);
            }
        }

        public static function assertNull(value:*, message:String = "Expected null"):void {
            if (value !== null) {
                throw new AssertionError(message + ": got " + value, "null", String(value), true);
            }
        }

        public static function assertNotNull(value:*, message:String = "Expected non-null"):void {
            if (value === null || value === undefined) {
                throw new AssertionError(message);
            }
        }

        public static function assertApprox(
            expected:Number,
            actual:Number,
            tolerance:Number = 0.0001,
            message:String = "Values differ"
        ):void {
            if (isNaN(expected) || isNaN(actual) || Math.abs(expected - actual) > tolerance) {
                throw new AssertionError(
                    message + ": expected " + expected + " +/- " + tolerance + ", got " + actual,
                    String(expected),
                    String(actual),
                    true
                );
            }
        }

        public static function assertThrows(
            block:Function,
            expectedErrorName:String = "",
            message:String = "Expected error"
        ):void {
            var caught:* = null;
            var threw:Boolean = false;
            try {
                block();
            } catch (error:*) {
                threw = true;
                caught = error;
            }
            if (!threw) {
                var detail:String = expectedErrorName != "" ? message + ": " + expectedErrorName : message;
                throw new AssertionError(detail);
            }
            if (expectedErrorName != "" && !TestProtocol.matchesError(caught, expectedErrorName)) {
                throw new AssertionError(
                    message + ": expected " + expectedErrorName + ", got " + TestProtocol.describeError(caught),
                    expectedErrorName,
                    caught is Error ? (caught as Error).name : String(caught),
                    true
                );
            }
        }

        public static function fail(message:String = "Assertion failed"):void {
            throw new AssertionError(message);
        }
    }
}
