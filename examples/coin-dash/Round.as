package {
    // The round owns the clock. Rendering and physics cannot award points after the buzzer.
    public class Round {
        public static const READY:int = 0;
        public static const RUNNING:int = 1;
        public static const FINISHED:int = 2;
        public static const LENGTH:Number = 30;
        public static const PENALTY:Number = 3;
        public static const RECHARGE:Number = 1.05;
        public var phase:int = READY;
        public var seconds:Number = LENGTH;
        public var score:int = 0;
        public var gold:int = 0;
        public var boost:Number = 0;
        public var recharge:Number = 0;
        public var grace:Number = 0;

        public function start():void {
            phase = RUNNING;
            seconds = LENGTH;
            score = 0;
            gold = 0;
            boost = 0;
            recharge = 0;
            grace = 0;
        }

        public function tick(delta:Number):void {
            if (phase != RUNNING) return;
            boost = Math.max(0, boost - delta);
            recharge = Math.max(0, recharge - delta);
            grace = Math.max(0, grace - delta);
            spend(delta);
        }

        public function dash():Boolean {
            if (phase != RUNNING || recharge > 0) return false;
            boost = 0.18;
            recharge = RECHARGE;
            return true;
        }

        public function hit():Boolean {
            if (phase != RUNNING || boost > 0 || grace > 0) return false;
            grace = 0.85;
            spend(PENALTY);
            return true;
        }

        public function collect(value:int):void {
            if (phase != RUNNING) return;
            score += value;
            if (value == 3) gold++;
        }

        private function spend(amount:Number):void {
            seconds = Math.max(0, seconds - amount);
            if (seconds <= 0.000001) {
                seconds = 0;
                phase = FINISHED;
            }
        }
    }
}
