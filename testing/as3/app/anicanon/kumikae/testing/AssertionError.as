package app.anicanon.kumikae.testing {
    public class AssertionError extends Error {
        public var expected:String;
        public var actual:String;
        public var hasComparison:Boolean;

        public function AssertionError(
            message:String = "Assertion failed",
            expected:String = "",
            actual:String = "",
            hasComparison:Boolean = false
        ) {
            super(message);
            this.name = "AssertionError";
            this.expected = expected;
            this.actual = actual;
            this.hasComparison = hasComparison;
        }
    }
}
