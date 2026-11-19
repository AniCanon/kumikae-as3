package app.anicanon.kumikae.testing {
    // Explicit registration works in the CLI and Godot without reflection.
    public class Suite {
        public static var only:Vector.<String> = new Vector.<String>();
        public var beforeClass:Function = null;
        public var afterClass:Function = null;
        private var name:String;
        private var cases:Vector.<TestCase> = new Vector.<TestCase>();

        public function Suite(name:String) {
            this.name = name;
        }

        public function get suiteName():String {
            return name;
        }

        public function get testCases():Vector.<TestCase> {
            return cases;
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

        public function isSelected(label:String):Boolean {
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

        public function run():Boolean {
            if (cases.length == 0) {
                throw new Error("Cannot run an empty suite");
            }
            var selected:Vector.<TestCase> = new Vector.<TestCase>();
            var runnable:int = 0;
            for (var index:int = 0; index < cases.length; index++) {
                if (isSelected(cases[index].name)) {
                    selected.push(cases[index]);
                    if (!cases[index].ignored) {
                        runnable++;
                    }
                }
            }
            if (selected.length == 0) {
                return true;
            }
            var failed:int = 0;
            TestProtocol.emit("suiteStarted", { name: name }, "");
            var classSetupError:* = null;
            var classSetupFailed:Boolean = false;
            if (runnable > 0 && beforeClass != null) {
                try {
                    beforeClass();
                } catch (setupError:*) {
                    classSetupFailed = true;
                    classSetupError = setupError;
                }
            }
            for (var i:int = 0; i < selected.length; i++) {
                var test:TestCase = selected[i];
                if (test.ignored) {
                    TestProtocol.emit("testIgnored", { name: test.name, reason: test.ignoreReason }, "");
                    continue;
                }
                var startedAt:Number = new Date().getTime();
                var startMeta:Object = { name: test.name };
                if (test.allowEngineErrors) {
                    startMeta.allowEngineErrors = true;
                }
                TestProtocol.emit("testStarted", startMeta, "");
                var errorMessage:String = "";
                var failureError:* = null;
                var failedCase:Boolean = false;
                if (classSetupFailed) {
                    failedCase = true;
                    failureError = classSetupError;
                    errorMessage = "BeforeClass: " + TestProtocol.describeError(classSetupError);
                } else {
                    var setupOk:Boolean = true;
                    try {
                        test.setUp();
                    } catch (setUpError:*) {
                        setupOk = false;
                        failedCase = true;
                        failureError = setUpError;
                        errorMessage = TestProtocol.describeError(setUpError);
                    }
                    if (setupOk) {
                        var threw:Boolean = false;
                        var runError:* = null;
                        try {
                            test.run();
                        } catch (error:*) {
                            threw = true;
                            runError = error;
                        }
                        if (test.expectedError != "") {
                            if (!threw) {
                                failedCase = true;
                                failureError = new AssertionError("Expected " + test.expectedError + " to be thrown");
                                errorMessage = TestProtocol.describeError(failureError);
                            } else if (!TestProtocol.matchesError(runError, test.expectedError)) {
                                failedCase = true;
                                failureError = new AssertionError(
                                    "Expected " + test.expectedError + ", got " + TestProtocol.describeError(runError),
                                    test.expectedError,
                                    runError is Error ? (runError as Error).name : String(runError),
                                    true
                                );
                                errorMessage = TestProtocol.describeError(failureError);
                            }
                        } else if (threw) {
                            failedCase = true;
                            failureError = runError;
                            errorMessage = TestProtocol.describeError(runError);
                        }
                    }
                    try {
                        test.tearDown();
                    } catch (cleanupError:*) {
                        failedCase = true;
                        if (failureError == null) {
                            failureError = cleanupError;
                        }
                        var cleanupDetail:String = "Cleanup: " + TestProtocol.describeError(cleanupError);
                        errorMessage = errorMessage == "" ? cleanupDetail : errorMessage + "\n" + cleanupDetail;
                    }
                }
                var duration:int = int(new Date().getTime() - startedAt);
                if (duration < 0) {
                    duration = 0;
                }
                if (failedCase) {
                    failed++;
                    TestProtocol.emit("testFailed", TestProtocol.failureMeta(test.name, failureError), errorMessage);
                }
                TestProtocol.emit("testFinished", { name: test.name, duration: duration }, "");
            }
            if (runnable > 0 && afterClass != null) {
                try {
                    afterClass();
                } catch (afterError:*) {
                    failed++;
                    var hookName:String = "@AfterClass";
                    TestProtocol.emit("testStarted", { name: hookName }, "");
                    TestProtocol.emit(
                        "testFailed",
                        TestProtocol.failureMeta(hookName, afterError),
                        "AfterClass: " + TestProtocol.describeError(afterError)
                    );
                    TestProtocol.emit("testFinished", { name: hookName, duration: 0 }, "");
                }
            }
            TestProtocol.emit("suiteFinished", { name: name }, "");
            trace("KAS3 SUITE COMPLETE", selected.length, failed);
            return failed == 0;
        }
    }
}
