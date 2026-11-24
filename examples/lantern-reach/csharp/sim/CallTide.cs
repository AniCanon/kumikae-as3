using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class CallTide : IEnemyState {
    public void think(Creature actor, EnemySenses senses) {
        if (actor.cooldown > 0) return;
        actor.cooldown = senses.rules.enemies[actor.kind].period;
        senses.space.nearby(actor.x, actor.y, 4, senses.near);
        foreach (Creature ally in senses.near) ally.buff = 90;
        senses.spawn.place(Creature.SWARMER, actor.x, actor.y, 0);
    }
}
