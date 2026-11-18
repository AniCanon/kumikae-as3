package app.anicanon.kumikae.testing {
    // Override run and optionally cleanup; ordinary virtual dispatch works on every runtime.
    public class TestCase {
        public var name:String;

        public function TestCase(name:String) {
            this.name = name;
        }

        public function run():void {
            throw new Error("Override TestCase.run");
        }

        public function setUp():void {
        }

        public function tearDown():void {
        }
    }
}
