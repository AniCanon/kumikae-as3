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
