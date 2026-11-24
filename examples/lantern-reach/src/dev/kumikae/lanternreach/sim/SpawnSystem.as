package dev.kumikae.lanternreach.sim {
    public final class SpawnSystem {
        private var grid:TownGrid;
        private var pool:CreaturePool;
        private var random:RandomStream;
        private var remaining:int;

        public function SpawnSystem(map:TownGrid, creatures:CreaturePool, stream:RandomStream, population:int) {
            grid = map;
            pool = creatures;
            random = stream;
            remaining = population;
        }

        public function update():void {
            while (remaining > 0) {
                var x:Number = random.below(grid.width * 1024) / 1024.0;
                var y:Number = random.below(5 * 1024) / 1024.0;
                pool.spawn(x, y);
                remaining--;
            }
        }
    }
}
