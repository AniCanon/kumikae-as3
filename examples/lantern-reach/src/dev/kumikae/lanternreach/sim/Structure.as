package dev.kumikae.lanternreach.sim {
    public final class Structure implements IDamageable {
        public static const LAMP:int = 0;
        public static const WALL:int = 1;
        public static const HARPOON:int = 2;
        public static const BRAZIER:int = 3;
        public static const BELL:int = 4;
        public static const RIG:int = 5;
        public var cell:int;
        public var kind:int;
        public var hp:int;
        public var tier:int = 1;
        public var branch:int;
        public var spent:int;
        public var cooldown:int;
        public var policy:ITargeting;

        public function hurt(amount:int):void {
            hp = Math.max(0, hp - amount);
        }
    }
}
