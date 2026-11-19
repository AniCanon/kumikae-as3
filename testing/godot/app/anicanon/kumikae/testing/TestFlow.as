package app.anicanon.kumikae.testing {
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
    }
}
