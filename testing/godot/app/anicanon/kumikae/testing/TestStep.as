package app.anicanon.kumikae.testing {
    public class TestStep {
        public static const FRAMES:String = "frames";
        public static const PHYSICS:String = "physics";
        public static const SECONDS:String = "seconds";
        public static const UNTIL:String = "until";

        public var kind:String;
        public var remainingFrames:int;
        public var remainingSeconds:Number;
        public var predicate:Function;
        public var timeoutMs:int;
        public var startedAt:Number = -1;
        public var action:Function;

        public function TestStep(
            kind:String,
            action:Function,
            frames:int = 0,
            seconds:Number = 0,
            predicate:Function = null,
            timeoutMs:int = 5000
        ) {
            this.kind = kind;
            this.action = action;
            this.remainingFrames = frames < 1 ? 1 : frames;
            this.remainingSeconds = seconds < 0 ? 0 : seconds;
            this.predicate = predicate;
            this.timeoutMs = timeoutMs <= 0 ? 5000 : timeoutMs;
        }
    }
}
