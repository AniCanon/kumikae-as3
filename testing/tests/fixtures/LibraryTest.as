package {
    import app.anicanon.kumikae.testing.Assert;
    import app.anicanon.kumikae.testing.TestCase;
    import app.anicanon.kumikae.testing.Suite;

    public class LibraryTest {
        public static var cleaned:int = 0;

        public function LibraryTest() {
            var suite:Suite = new Suite("library");
            suite.add(new ProbeCase("pass", false));
            suite.add(new ProbeCase("failure", true));
            suite.add(new ProbeCase("continues 😀", false));
            var passed:Boolean = suite.run();
            if (passed || cleaned != 3) {
                throw new Error("Suite must continue and clean up after failures");
            }
            var hooks:Suite = new Suite("hooks");
            hooks.add(new ProbeCase("setup failure", false, "setup"));
            hooks.add(new ProbeCase("cleanup failure", false, "cleanup"));
            hooks.add(new ProbeCase("after failures", false));
            if (hooks.run() || cleaned != 6 || ProbeCase.runs != 5) {
                throw new Error("Hooks must preserve failure and cleanup contracts");
            }
            trace("PASS framework lifecycle");
        }

    }

}
