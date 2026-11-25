package dev.kumikae.lanternreach.sim {
    public final class Keeper implements IMover {
        public var aimX:Number = 1;
        public var aimY:Number = 0;
        public var firing:Boolean;
        public var dash:int;
        public var recharge:int;
        public var speed:Number = 1;
        public var lastShot:int;
        public var damage:int;
        public var range:int;
        public var pull:Number = 2.5;
        public var haste:int;
        public var orbit:int;
        public var spark:int;
        public var xp:int;
        public var level:int = 1;
        public var offers:Vector.<int> = new Vector.<int>();
        public var x:Number = 32;
        public var y:Number = 20;
        public var dx:Number = 0;
        public var dy:Number = 0;
        public var salvage:int = 120;

        public function move(grid:TownGrid):void {
            if (recharge > 0) recharge--;
            if (dash > 0) dash--;
            var stride:Number = speed * (dash > 0 ? 0.32 : 0.08) * (grid.marsh[grid.cell(x, y)] ? 0.6 : 1);
            var length:Number = Math.max(1, Math.sqrt(dx * dx + dy * dy));
            var px:Number = Math.max(0, Math.min(grid.width - 0.001, x + dx * stride / length));
            var py:Number = Math.max(0, Math.min(grid.height - 0.001, y + dy * stride / length));
            if (!grid.walls[grid.cell(px, y)]) x = px;
            if (!grid.walls[grid.cell(x, py)]) y = py;
        }
    }
}
