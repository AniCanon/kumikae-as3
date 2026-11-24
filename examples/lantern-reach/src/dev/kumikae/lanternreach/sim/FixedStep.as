package dev.kumikae.lanternreach.sim {
    public final class FixedStep {
        private var accumulated:Number = 0;

        public function advance(delta:Number):int {
            accumulated += delta * 60;
            var ticks:int = int(accumulated);
            accumulated -= ticks;
            return ticks;
        }
    }
}
