package app.anicanon.kumikae.testing {
    import godot.Node;
    import godot.PackedScene;
    import godot.Resource;
    import godot.ResourceLoader;

    // Isolated per-test scene sandbox and multi-frame step scheduler for Godot tests.
    public class TestScene {
        private static var host:Node = null;
        private static var sandbox:Node = null;
        private static var touched:Boolean = false;
        private static var steps:Vector.<TestStep> = new Vector.<TestStep>();

        public static function get root():Node {
            if (host == null) {
                throw new Error("TestScene is only available inside a Godot engine test run.");
            }
            if (sandbox == null) {
                sandbox = new Node();
                host.addChild(sandbox);
            }
            touched = true;
            return sandbox;
        }

        public static function instantiate(scenePath:String):Node {
            var resource:Resource = ResourceLoader.load(scenePath);
            if (resource == null || !(resource is PackedScene)) {
                throw new AssertionError("Scene not found: " + scenePath);
            }
            return add((resource as PackedScene).instantiate());
        }

        public static function add(node:Node):Node {
            if (node == null) {
                throw new AssertionError("Cannot add a null node to TestScene");
            }
            root.addChild(node);
            return node;
        }

        public static function afterFrames(count:int, action:Function):TestFlow {
            return flow().afterFrames(count, action);
        }

        public static function afterPhysicsFrames(count:int, action:Function):TestFlow {
            return flow().afterPhysicsFrames(count, action);
        }

        public static function afterSeconds(seconds:Number, action:Function):TestFlow {
            return flow().afterSeconds(seconds, action);
        }

        public static function until(predicate:Function, timeoutMs:int, action:Function):TestFlow {
            return flow().until(predicate, timeoutMs, action);
        }

        public static function beginTest(runnerHost:Node):void {
            host = runnerHost;
            sandbox = null;
            touched = false;
            steps = new Vector.<TestStep>();
        }

        public static function hasPendingSteps():Boolean {
            return steps.length > 0;
        }

        public static function peekStep():TestStep {
            return steps.length > 0 ? steps[0] : null;
        }

        public static function shiftStep():TestStep {
            if (steps.length == 0) {
                return null;
            }
            var first:TestStep = steps[0];
            steps.splice(0, 1);
            return first;
        }

        public static function clearSteps():void {
            steps = new Vector.<TestStep>();
        }

        public static function finishTest():Boolean {
            var needsFlush:Boolean = touched;
            if (sandbox != null) {
                if (host != null) {
                    host.removeChild(sandbox);
                }
                sandbox.queueFree();
                sandbox = null;
            }
            steps = new Vector.<TestStep>();
            touched = false;
            return needsFlush;
        }

        private static function flow():TestFlow {
            if (host == null) {
                throw new Error("TestScene is only available inside a Godot engine test run.");
            }
            return new TestFlow(steps);
        }
    }
}
