package dev.kumikae.lanternreach.sim {
    public final class Creature implements IDamageable {
        public static const SWARMER:int = 0;
        public static const BRUTE:int = 1;
        public static const WISP:int = 2;
        public static const SPLITTER:int = 3;
        public static const LURKER:int = 4;
        public static const TIDECALLER:int = 5;
        public static const UNDERTOW:int = 6;
        public var kind:int;
        public var generation:int;
        public var burn:int;
        public var slow:int;
        public var buff:int;
        public var cooldown:int;
        public var warning:int;
        public var flood:int;
        public var phase:int;
        public var lit:Boolean;
        public var speed:Number;
        public var id:int;

        public function hurt(amount:int):void {
            if (kind != LURKER || lit) hp = Math.max(0, hp - amount);
        }
        public var active:Boolean;
        public var x:Number;
        public var y:Number;
        public var vx:Number;
        public var vy:Number;
        public var steerX:Number;
        public var steerY:Number;
        public var hp:int;
        public var nextInCell:int;

        public function reset(slot:int, px:Number, py:Number):void {
            id = slot;
            x = px;
            y = py;
            vx = 0;
            vy = 0;
            steerX = 0;
            steerY = 0;
            hp = 10;
            kind = SWARMER;
            generation = 0;
            burn = 0;
            slow = 0;
            buff = 0;
            warning = 0;
            phase = 0;
            flood = 0;
            cooldown = slot % 60;
            speed = 0.05;
            lit = false;
            nextInCell = -1;
            active = true;
        }
    }
}
