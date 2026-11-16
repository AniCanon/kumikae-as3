package kumikae.testing {
    // Explicit registration works in the CLI and Godot without reflection.
    public class Suite {
        public static var only:Vector.<String> = new Vector.<String>();
        private var name:String;
        private var cases:Vector.<TestCase> = new Vector.<TestCase>();

        public function Suite(name:String) {
            this.name = name;
        }

        public function add(test:TestCase):void {
            if (test == null || test.name == "") {
                throw new Error("A test needs a name");
            }
            for (var i:int = 0; i < cases.length; i++) {
                if (cases[i].name == test.name) {
                    throw new Error("Duplicate test: " + test.name);
                }
            }
            cases.push(test);
        }

        public function run():Boolean {
            if (cases.length == 0) {
                throw new Error("Cannot run an empty suite");
            }
            var selected:int = 0;
            for (var index:int = 0; index < cases.length; index++) {
                if (selectedCase(cases[index].name)) {
                    selected++;
                }
            }
            if (selected == 0) {
                return true;
            }
            var failed:int = 0;
            emit("suiteStarted", name, "");
            for (var i:int = 0; i < cases.length; i++) {
                var test:TestCase = cases[i];
                if (!selectedCase(test.name)) {
                    continue;
                }
                var errorMessage:String = "";
                var failedCase:Boolean = false;
                emit("testStarted", test.name, "");
                try {
                    test.setUp();
                    test.run();
                } catch (error:*) {
                    failedCase = true;
                    errorMessage = error is Error ? (error as Error).message : String(error);
                }
                try {
                    test.tearDown();
                } catch (cleanupError:*) {
                    failedCase = true;
                    errorMessage += " Cleanup: " + String(cleanupError);
                }
                if (failedCase) {
                    failed++;
                    emit("testFailed", test.name, errorMessage);
                }
                emit("testFinished", test.name, "");
            }
            emit("suiteFinished", name, "");
            trace("KAS3 SUITE COMPLETE", selected, failed);
            return failed == 0;
        }

        private function selectedCase(label:String):Boolean {
            if (only.length == 0) {
                return true;
            }
            for (var i:int = 0; i < only.length; i++) {
                if (only[i] == name + "::" + label) {
                    return true;
                }
            }
            return false;
        }

        private function emit(event:String, label:String, message:String):void {
            trace("@kas3-test-v1", event, label.length, message.length);
            trace(label);
            trace(message);
        }
    }
}
