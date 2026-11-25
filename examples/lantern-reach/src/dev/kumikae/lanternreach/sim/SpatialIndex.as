package dev.kumikae.lanternreach.sim {
    public final class SpatialIndex {
        private var grid:TownGrid;
        private var pool:CreaturePool;
        public var head:Vector.<int> = new Vector.<int>();

        public function SpatialIndex(map:TownGrid, creatures:CreaturePool) {
            grid = map;
            pool = creatures;
            for (var i:int = 0; i < grid.walls.length; i++) head.push(-1);
        }

        public function nearby(px:Number, py:Number, radius:Number, result:Vector.<Creature>):void {
            result.length = 0;
            var top:int = int(Math.max(0, py - radius));
            var bottom:int = int(Math.min(grid.height - 1, py + radius));
            var left:int = int(Math.max(0, px - radius));
            var right:int = int(Math.min(grid.width - 1, px + radius));
            for (var y:int = top; y <= bottom; y++) {
                for (var x:int = left; x <= right; x++) {
                    var id:int = head[y * grid.width + x];
                    while (id >= 0) {
                        var enemy:Creature = pool.slots[id];
                        id = enemy.nextInCell;
                        var dx:Number = px - enemy.x;
                        var dy:Number = py - enemy.y;
                        if (enemy.active && dx * dx + dy * dy <= radius * radius) result.push(enemy);
                    }
                }
            }
        }

        public function rebuild():void {
            for (var i:int = 0; i < head.length; i++) head[i] = -1;
            for (var slot:int = pool.slots.length - 1; slot >= 0; slot--) {
                var actor:Creature = pool.slots[slot];
                if (!actor.active) continue;
                var cellId:int = grid.cell(actor.x, actor.y);
                actor.nextInCell = head[cellId];
                head[cellId] = slot;
            }
        }
    }
}
