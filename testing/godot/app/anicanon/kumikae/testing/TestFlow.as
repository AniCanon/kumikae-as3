package app.anicanon.kumikae.testing {
    import godot.Signal;

    public class TestFlow {
        private var queue:Vector.<TestStep>;

        public function TestFlow(queue:Vector.<TestStep>) {
            this.queue = queue;
        }

        public function afterFrames(count:int, action:Function):TestFlow {
            queue.push(new TestStep(TestStep.FRAMES, action, count));
            return this;
        }

        public function afterPhysicsFrames(count:int, action:Function):TestFlow {
            queue.push(new TestStep(TestStep.PHYSICS, action, count));
            return this;
        }

        public function afterSeconds(seconds:Number, action:Function):TestFlow {
            queue.push(new TestStep(TestStep.SECONDS, action, 0, seconds));
            return this;
        }

        public function until(predicate:Function, timeoutMs:int, action:Function):TestFlow {
            queue.push(new TestStep(TestStep.UNTIL, action, 0, 0, predicate, timeoutMs));
            return this;
        }

        public function waitForSignal(signal:Signal, timeoutMs:int, action:Function):TestFlow {
            if (signal == null) {
                throw new AssertionError("Cannot wait on a null Signal");
            }
            var fired:Boolean = false;
            var detached:Boolean = false;
            var listener:Function = null;
            var detach:Function = function():void {
                if (!detached) {
                    detached = true;
                    signal.remove(listener);
                }
            };
            listener = function(...rest):void {
                fired = true;
                detach();
            };
            signal.add(listener);
            TestScene.onCleanup(detach);
            return until(function():Boolean {
                return fired;
            }, timeoutMs, function():void {
                detach();
                action();
            });
        }
    }
}
