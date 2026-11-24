package dev.kumikae.lanternreach.sim {
    public final class RunStats {
        public var built:int;
        public var destroyed:int;
        public var deaths:int;
        public var waves:int;
        public var lost:int;
        private var subscription:Subscription;

        public function RunStats(bus:EventBus) {
            subscription = bus.listen(receive);
        }

        private function receive(event:WorldEvent):void {
            if (event.kind == WorldEvent.BUILT) built++;
            if (event.kind == WorldEvent.DESTROYED) destroyed++;
            if (event.kind == WorldEvent.DEATH) deaths++;
            if (event.kind == WorldEvent.WAVE_START) waves++;
            if (event.kind == WorldEvent.BEACON_LOST) lost++;
        }

        public function close(bus:EventBus):void {
            bus.remove(subscription);
        }
    }
}
