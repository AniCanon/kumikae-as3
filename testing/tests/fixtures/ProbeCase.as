package {
    import kumikae.testing.TestCase;
    import kumikae.testing.Assert;
    public class ProbeCase extends TestCase {
        public static var runs:int = 0;
        private var fail:Boolean;
        private var phase:String;

        public function ProbeCase(name:String, fail:Boolean, phase:String = "body") {
            super(name);
            this.fail = fail;
            this.phase = phase;
        }

        override public function setUp():void {
            if (phase == "setup") {
                throw new Error("Setup failed");
            }
        }

        override public function run():void {
            runs++;
            Assert.assertEquals(3, 1 + 2, "Arithmetic assertion");
            Assert.assertTrue(!fail, "");
        }

        override public function tearDown():void {
            LibraryTest.cleaned++;
            if (phase == "cleanup") {
                throw new Error("Cleanup failed");
            }
        }
    }
}
