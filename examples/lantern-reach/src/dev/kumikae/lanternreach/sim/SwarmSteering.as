package dev.kumikae.lanternreach.sim {
    public final class SwarmSteering {
        private var grid:TownGrid;
        private var pool:CreaturePool;
        private var index:SpatialIndex;
        private var flow:FlowField;

        public function SwarmSteering(map:TownGrid, creatures:CreaturePool, space:SpatialIndex, paths:FlowField) {
            grid = map;
            pool = creatures;
            index = space;
            flow = paths;
        }

        public function update():void {
            for (var i:int = 0; i < pool.slots.length; i++) {
                var actor:Creature = pool.slots[i];
                if (actor.active) steer(actor);
            }
        }

        private function steer(actor:Creature):void {
            var cx:int = int(actor.x);
            var cy:int = int(actor.y);
            var repelX:Number = 0;
            var repelY:Number = 0;
            var alignX:Number = 0;
            var alignY:Number = 0;
            var centreX:Number = 0;
            var centreY:Number = 0;
            var count:int = 0;
            for (var y:int = Math.max(0, cy - 1); y <= Math.min(grid.height - 1, cy + 1); y++) {
                for (var x:int = Math.max(0, cx - 1); x <= Math.min(grid.width - 1, cx + 1); x++) {
                    var id:int = index.head[y * grid.width + x];
                    while (id >= 0) {
                        var near:Creature = pool.slots[id];
                        id = near.nextInCell;
                        var dx:Number = actor.x - near.x;
                        var dy:Number = actor.y - near.y;
                        var squared:Number = dx * dx + dy * dy;
                        if (squared == 0 || squared > 1) continue;
                        count++;
                        centreX -= dx;
                        centreY -= dy;
                        alignX += near.vx;
                        alignY += near.vy;
                        if (squared < 0.25) {
                            repelX += dx / (0.04 + squared);
                            repelY += dy / (0.04 + squared);
                        }
                    }
                }
            }
            var target:int = flow.toward[grid.cell(actor.x, actor.y)];
            var tx:Number = target >= 0 ? target % grid.width + 0.5 - actor.x : 0;
            var ty:Number = target >= 0 ? int(target / grid.width) + 0.5 - actor.y : 0;
            var divisor:int = Math.max(1, count);
            actor.steerX = tx + repelX * 0.25 + (alignX * 0.2 + centreX * 0.1) / divisor;
            actor.steerY = ty + repelY * 0.25 + (alignY * 0.2 + centreY * 0.1) / divisor;
        }
    }
}
