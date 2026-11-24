package dev.kumikae.lanternreach.sim {
    public final class WakeWrecks implements IEnemyState {
        public function think(actor:Creature, senses:EnemySenses):void {
            for (var i:int = 0; i < 4; i++) {
                var x:Number = Math.min(senses.grid.width - 0.001, actor.x + i * 0.1);
                senses.spawn.place(Creature.SPLITTER, x, actor.y, 0);
            }
        }
    }
}
