package dev.kumikae.lanternreach.sim {
    public final class ChainSweep implements IEnemyState {
        public function think(actor:Creature, senses:EnemySenses):void {
            var damage:int = senses.rules.enemies[actor.kind].damage;
            for (var i:int = senses.town.placed.length - 1; i >= 0; i--) {
                var tower:Structure = senses.town.placed[i];
                if (senses.distance(actor, tower.cell) < 36) {
                    tower.hurt(damage);
                    if (tower.hp == 0) senses.town.remove(tower.cell, false);
                }
            }
        }
    }
}
