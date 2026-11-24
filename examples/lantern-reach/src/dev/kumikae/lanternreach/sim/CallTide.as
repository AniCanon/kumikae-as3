package dev.kumikae.lanternreach.sim {
    public final class CallTide implements IEnemyState {
        public function think(actor:Creature, senses:EnemySenses):void {
            if (actor.cooldown > 0) return;
            actor.cooldown = senses.rules.enemies[actor.kind].period;
            senses.space.nearby(actor.x, actor.y, 4, senses.near);
            for each (var ally:Creature in senses.near) ally.buff = 90;
            senses.spawn.place(Creature.SWARMER, actor.x, actor.y, 0);
        }
    }
}
