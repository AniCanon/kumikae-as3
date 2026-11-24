package dev.kumikae.lanternreach.sim {
    public final class WorldEvent {
        public static const DEATH:int = 0;
        public static const DAMAGE:int = 1;
        public static const LEVEL:int = 2;
        public static const WAVE_START:int = 3;
        public static const WAVE_END:int = 4;
        public static const BEACON_LOST:int = 5;
        public static const BUILT:int = 6;
        public static const DESTROYED:int = 7;
        public static const SWING:int = 10;
        public static const FIRED:int = 9;
        public static const REFUSED:int = 8;
        public var kind:int;
        public var cell:int;
        public var value:int;

        public function WorldEvent(type:int, tile:int, amount:int) {
            kind = type;
            cell = tile;
            value = amount;
        }
    }
}
