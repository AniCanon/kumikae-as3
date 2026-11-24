package dev.kumikae.lanternreach.sim {
    public final class FloodMarsh implements IEnemyState {
        public function think(actor:Creature, senses:EnemySenses):void {
            var grid:TownGrid = senses.grid;
            var row:int = (int(actor.y) + actor.flood + 1) % grid.height;
            actor.flood++;
            for (var x:int = 0; x < grid.width; x++) grid.marsh[row * grid.width + x] = true;
            grid.revision++;
        }
    }
}
