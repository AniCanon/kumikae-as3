package dev.kumikae.lanternreach.sim {
    public final class Keeper {
        public var x:Number = 32;
        public var y:Number = 20;
        public var dx:Number = 0;
        public var dy:Number = 0;
        public var salvage:int = 120;

        public function move(grid:TownGrid):void {
            var px:Number = Math.max(0, Math.min(grid.width - 0.001, x + dx * 0.08));
            var py:Number = Math.max(0, Math.min(grid.height - 0.001, y + dy * 0.08));
            if (!grid.walls[grid.cell(px, y)]) x = px;
            if (!grid.walls[grid.cell(x, py)]) y = py;
        }
    }
}
