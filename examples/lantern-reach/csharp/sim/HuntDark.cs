using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class HuntDark : IEnemyState {
    public void think(Creature actor, EnemySenses senses) {
        double best = 256;
        Structure target = null;
        foreach (Structure tower in senses.town.placed) {
            if (senses.light.level[tower.cell] > 0) continue;
            double score = senses.distance(actor, tower.cell);
            if (score < best) {
                best = score;
                target = tower;
            }
        }
        if (target != null) {
            senses.seek(actor, target.cell);
            senses.strike(actor, target);
        }
    }
}
