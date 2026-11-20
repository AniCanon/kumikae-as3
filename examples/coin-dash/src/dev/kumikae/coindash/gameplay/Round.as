package dev.kumikae.coindash.gameplay {
    import dev.kumikae.coindash.config.RoundRules;

    // The round owns the clock. Rendering and physics cannot award points after the buzzer.
    public class Round {
        public static const READY:int = 0;
        public static const RUNNING:int = 1;
        public static const FINISHED:int = 2;
        private static const CLOCK_EPSILON:Number = 0.000001;

        public var phase:int = READY;
        public var seconds:Number = RoundRules.ROUND_SECONDS;
        public var score:int = 0;
        public var silver:int = 0;
        public var gold:int = 0;
        public var boost:Number = 0;
        public var recharge:Number = 0;
        public var grace:Number = 0;

        public function start():void {
            phase = RUNNING;
            seconds = RoundRules.ROUND_SECONDS;
            score = 0;
            silver = 0;
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
            boost = RoundRules.DASH_SECONDS;
            recharge = RoundRules.DASH_RECHARGE_SECONDS;
            return true;
        }

        public function hit():Boolean {
            if (phase != RUNNING || boost > 0 || grace > 0) return false;
            grace = RoundRules.HIT_GRACE_SECONDS;
            spend(RoundRules.HIT_PENALTY_SECONDS);
            return true;
        }

        public function collectSilver():void {
            if (phase != RUNNING) return;
            silver++;
            score += RoundRules.SILVER_POINTS;
        }

        public function collectGold():void {
            if (phase != RUNNING) return;
            gold++;
            score += RoundRules.GOLD_POINTS;
        }

        private function spend(amount:Number):void {
            seconds = Math.max(0, seconds - amount);
            if (seconds <= CLOCK_EPSILON) {
                seconds = 0;
                phase = FINISHED;
            }
        }
    }
}
