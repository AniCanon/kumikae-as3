package app.anicanon.kumikae.testing {
    import godot.Node;
    import godot.Time;

    // Drives AS3 Suites inside a live Godot SceneTree across _process and _physicsProcess frames.
    public class EngineTestRunner extends Node {
        private static const STATE_NEXT_SUITE:int = 0;
        private static const STATE_NEXT_CASE:int = 1;
        private static const STATE_WAITING_STEPS:int = 2;
        private static const STATE_CLEANUP_FLUSH:int = 3;
        private static const STATE_DONE:int = 4;
        private static const DEFAULT_TIMEOUT_MS:int = 30000;
        private static const MAX_SYNC_PER_FRAME:int = 64;

        protected var completionMarker:String = "PASS discovered suite";
        private var suites:Vector.<Suite> = new Vector.<Suite>();
        private var state:int = STATE_NEXT_SUITE;
        private var suiteIndex:int = 0;
        private var activeSuite:Suite = null;
        private var selectedCases:Vector.<TestCase> = null;
        private var caseIndex:int = 0;
        private var suiteFailed:int = 0;
        private var suiteRunnable:int = 0;
        private var classSetupFailed:Boolean = false;
        private var classSetupError:* = null;
        private var anyFailed:Boolean = false;

        private var activeCase:TestCase = null;
        private var caseStartedAt:Number = 0;
        private var caseFailed:Boolean = false;
        private var caseFailureError:* = null;
        private var caseErrorMessage:String = "";
        private var caseThrew:Boolean = false;
        private var caseThrownError:* = null;

        protected function addSuite(suite:Suite):void {
            suites.push(suite);
        }

        protected function registerSuites():void {
        }

        override public function _ready():void {
            // Run after scene nodes (default priority 0) so afterFrames(N) sees N completed ticks.
            processPriority = 10000;
            processPhysicsPriority = 10000;
            registerSuites();
            state = STATE_NEXT_SUITE;
            suiteIndex = 0;
        }

        override public function _process(delta:Number):void {
            if (state == STATE_DONE) {
                return;
            }
            if (state == STATE_CLEANUP_FLUSH) {
                state = STATE_NEXT_CASE;
            }
            if (state == STATE_WAITING_STEPS) {
                tickProcessStep(delta);
                if (state == STATE_WAITING_STEPS || state == STATE_CLEANUP_FLUSH || state == STATE_DONE) {
                    return;
                }
            }
            var budget:int = MAX_SYNC_PER_FRAME;
            while (budget > 0 && (state == STATE_NEXT_SUITE || state == STATE_NEXT_CASE)) {
                budget--;
                if (state == STATE_NEXT_SUITE) {
                    advanceSuite();
                } else {
                    advanceCase();
                }
            }
        }

        override public function _physicsProcess(delta:Number):void {
            if (state != STATE_WAITING_STEPS) {
                return;
            }
            var step:TestStep = TestScene.peekStep();
            if (step == null || step.kind != TestStep.PHYSICS) {
                return;
            }
            step.remainingFrames--;
            if (step.remainingFrames <= 0) {
                runStep(TestScene.shiftStep());
            }
        }

        private function advanceSuite():void {
            if (suiteIndex >= suites.length) {
                finishAll();
                return;
            }
            activeSuite = suites[suiteIndex];
            suiteIndex++;
            var cases:Vector.<TestCase> = activeSuite.testCases;
            if (cases.length == 0) {
                anyFailed = true;
                throw new Error("Cannot run an empty suite: " + activeSuite.suiteName);
            }
            selectedCases = new Vector.<TestCase>();
            suiteRunnable = 0;
            for (var i:int = 0; i < cases.length; i++) {
                if (activeSuite.isSelected(cases[i].name)) {
                    selectedCases.push(cases[i]);
                    if (!cases[i].ignored) {
                        suiteRunnable++;
                    }
                }
            }
            if (selectedCases.length == 0) {
                return;
            }
            suiteFailed = 0;
            caseIndex = 0;
            classSetupFailed = false;
            classSetupError = null;
            TestProtocol.emit("suiteStarted", { name: activeSuite.suiteName }, "");
            if (suiteRunnable > 0 && activeSuite.beforeClass != null) {
                try {
                    activeSuite.beforeClass();
                } catch (setupError:*) {
                    classSetupFailed = true;
                    classSetupError = setupError;
                }
            }
            state = STATE_NEXT_CASE;
        }

        private function advanceCase():void {
            if (caseIndex >= selectedCases.length) {
                finishSuite();
                return;
            }
            activeCase = selectedCases[caseIndex];
            caseIndex++;
            if (activeCase.ignored) {
                TestProtocol.emit("testIgnored", { name: activeCase.name, reason: activeCase.ignoreReason }, "");
                activeCase = null;
                return;
            }
            caseStartedAt = nowMs();
            caseFailed = false;
            caseFailureError = null;
            caseErrorMessage = "";
            caseThrew = false;
            caseThrownError = null;
            var startMeta:Object = { name: activeCase.name };
            if (activeCase.allowEngineErrors) {
                startMeta.allowEngineErrors = true;
            }
            TestProtocol.emit("testStarted", startMeta, "");
            TestScene.beginTest(this);
            if (classSetupFailed) {
                caseFailed = true;
                caseFailureError = classSetupError;
                caseErrorMessage = "BeforeClass: " + TestProtocol.describeError(classSetupError);
                completeActiveCase(false);
                return;
            }
            var setupOk:Boolean = true;
            try {
                activeCase.setUp();
            } catch (setUpError:*) {
                setupOk = false;
                caseFailed = true;
                caseFailureError = setUpError;
                caseErrorMessage = TestProtocol.describeError(setUpError);
            }
            if (!setupOk) {
                completeActiveCase(true);
                return;
            }
            try {
                activeCase.run();
            } catch (error:*) {
                caseThrew = true;
                caseThrownError = error;
                TestScene.clearSteps();
            }
            if (!caseThrew && TestScene.hasPendingSteps()) {
                state = STATE_WAITING_STEPS;
                return;
            }
            evaluateExpectedAndComplete();
        }

        private function tickProcessStep(delta:Number):void {
            var now:Number = nowMs();
            var limit:int = activeCase.timeoutMs > 0 ? activeCase.timeoutMs : DEFAULT_TIMEOUT_MS;
            if (now - caseStartedAt >= limit) {
                TestScene.clearSteps();
                caseFailed = true;
                caseFailureError = new AssertionError("Test timed out after " + limit + " ms");
                caseErrorMessage = TestProtocol.describeError(caseFailureError);
                completeActiveCase(true);
                return;
            }
            var step:TestStep = TestScene.peekStep();
            if (step == null) {
                evaluateExpectedAndComplete();
                return;
            }
            if (step.kind == TestStep.FRAMES) {
                step.remainingFrames--;
                if (step.remainingFrames <= 0) {
                    runStep(TestScene.shiftStep());
                }
            } else if (step.kind == TestStep.SECONDS) {
                step.remainingSeconds -= delta;
                if (step.remainingSeconds <= 0) {
                    runStep(TestScene.shiftStep());
                }
            } else if (step.kind == TestStep.UNTIL) {
                if (step.startedAt < 0) {
                    step.startedAt = now;
                }
                var ready:Boolean = false;
                try {
                    ready = Boolean(step.predicate());
                } catch (predicateError:*) {
                    TestScene.clearSteps();
                    caseThrew = true;
                    caseThrownError = predicateError;
                    evaluateExpectedAndComplete();
                    return;
                }
                if (ready) {
                    runStep(TestScene.shiftStep());
                } else if (now - step.startedAt >= step.timeoutMs) {
                    TestScene.clearSteps();
                    caseFailed = true;
                    caseFailureError = new AssertionError("Condition timed out after " + step.timeoutMs + " ms");
                    caseErrorMessage = TestProtocol.describeError(caseFailureError);
                    completeActiveCase(true);
                }
            }
        }

        private function runStep(step:TestStep):void {
            if (step == null) {
                return;
            }
            try {
                step.action();
            } catch (error:*) {
                caseThrew = true;
                caseThrownError = error;
                TestScene.clearSteps();
            }
            if (caseThrew || !TestScene.hasPendingSteps()) {
                evaluateExpectedAndComplete();
            }
        }

        private function evaluateExpectedAndComplete():void {
            if (!caseFailed) {
                if (activeCase.expectedError != "") {
                    if (!caseThrew) {
                        caseFailed = true;
                        caseFailureError = new AssertionError("Expected " + activeCase.expectedError + " to be thrown");
                        caseErrorMessage = TestProtocol.describeError(caseFailureError);
                    } else if (!TestProtocol.matchesError(caseThrownError, activeCase.expectedError)) {
                        caseFailed = true;
                        caseFailureError = new AssertionError(
                            "Expected " + activeCase.expectedError + ", got " + TestProtocol.describeError(caseThrownError),
                            activeCase.expectedError,
                            caseThrownError is Error ? (caseThrownError as Error).name : String(caseThrownError),
                            true
                        );
                        caseErrorMessage = TestProtocol.describeError(caseFailureError);
                    }
                } else if (caseThrew) {
                    caseFailed = true;
                    caseFailureError = caseThrownError;
                    caseErrorMessage = TestProtocol.describeError(caseThrownError);
                }
            }
            completeActiveCase(true);
        }

        private function completeActiveCase(runTearDown:Boolean):void {
            if (runTearDown) {
                try {
                    activeCase.tearDown();
                } catch (cleanupError:*) {
                    caseFailed = true;
                    if (caseFailureError == null) {
                        caseFailureError = cleanupError;
                    }
                    var detail:String = "Cleanup: " + TestProtocol.describeError(cleanupError);
                    caseErrorMessage = caseErrorMessage == "" ? detail : caseErrorMessage + "\n" + detail;
                }
            }
            var needsFlush:Boolean = TestScene.finishTest();
            var duration:int = int(nowMs() - caseStartedAt);
            if (duration < 0) {
                duration = 0;
            }
            if (caseFailed) {
                suiteFailed++;
                anyFailed = true;
                TestProtocol.emit("testFailed", TestProtocol.failureMeta(activeCase.name, caseFailureError), caseErrorMessage);
            }
            TestProtocol.emit("testFinished", { name: activeCase.name, duration: duration }, "");
            activeCase = null;
            state = needsFlush ? STATE_CLEANUP_FLUSH : STATE_NEXT_CASE;
        }

        private function finishSuite():void {
            if (suiteRunnable > 0 && activeSuite.afterClass != null) {
                try {
                    activeSuite.afterClass();
                } catch (afterError:*) {
                    suiteFailed++;
                    anyFailed = true;
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
            TestProtocol.emit("suiteFinished", { name: activeSuite.suiteName }, "");
            trace("KAS3 SUITE COMPLETE", selectedCases.length, suiteFailed);
            activeSuite = null;
            selectedCases = null;
            state = STATE_NEXT_SUITE;
        }

        private function finishAll():void {
            state = STATE_DONE;
            trace(completionMarker);
            getTree().quit(anyFailed ? 1 : 0);
        }

        private function nowMs():Number {
            return Number(Time.getTicksMsec());
        }
    }
}
