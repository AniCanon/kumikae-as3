package dev.kumikae.lanternreach.sim {
    public final class Movement {
        private var grid:TownGrid;
        private var pool:CreaturePool;

        public function Movement(map:TownGrid, creatures:CreaturePool) {
            grid = map;
            pool = creatures;
        }

        public function update():void {
            for (var i:int = 0; i < pool.slots.length; i++) {
                var actor:Creature = pool.slots[i];
                if (!actor.active) continue;
                var length:Number = Math.sqrt(actor.steerX * actor.steerX + actor.steerY * actor.steerY);
                var scale:Number = (grid.marsh[grid.cell(actor.x, actor.y)] ? 0.025 : 0.05) / Math.max(1, length);
                actor.vx = actor.steerX * scale;
                actor.vy = actor.steerY * scale;
                var px:Number = Math.max(0, Math.min(grid.width - 0.001, actor.x + actor.vx));
                var py:Number = Math.max(0, Math.min(grid.height - 0.001, actor.y + actor.vy));
                if (!grid.walls[grid.cell(px, actor.y)]) actor.x = px;
                if (!grid.walls[grid.cell(actor.x, py)]) actor.y = py;
            }
        }
    }
}
