using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class WakeWrecks : IEnemyState {
    public void think(Creature actor, EnemySenses senses) {
        for (int i = 0; i < 4; i++) {
            double x = Math.Min(senses.grid.width - 0.001, actor.x + i * 0.1);
            senses.spawn.place(Creature.SPLITTER, x, actor.y, 0);
        }
    }
}
