using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class ChainSweep : IEnemyState {
    public void think(Creature actor, EnemySenses senses) {
        int damage = senses.rules.enemies[actor.kind].damage;
        for (int i = senses.town.placed.Count - 1; i >= 0; i--) {
            Structure tower = senses.town.placed[i];
            if (senses.distance(actor, tower.cell) < 36) {
                tower.hurt(damage);
                if (tower.hp == 0) senses.town.remove(tower.cell, false);
            }
        }
    }
}
