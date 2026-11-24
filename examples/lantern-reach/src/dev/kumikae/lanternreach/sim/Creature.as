package dev.kumikae.lanternreach.sim {
    public final class Creature {
        public var id:int;
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
            nextInCell = -1;
            active = true;
        }
    }
}
