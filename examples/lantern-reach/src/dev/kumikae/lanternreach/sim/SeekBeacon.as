package dev.kumikae.lanternreach.sim {
    public final class SeekBeacon implements IEnemyState {
        public function think(actor:Creature, senses:EnemySenses):void {
            var tile:int = senses.grid.cell(actor.x, actor.y);
            senses.strike(actor, senses.town.at[tile]);
        }
    }
}
