using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class FloodMarsh : IEnemyState {
    public void think(Creature actor, EnemySenses senses) {
        TownGrid grid = senses.grid;
        int row = ((int)(actor.y) + actor.flood + 1) % grid.height;
        actor.flood++;
        grid.floods++;
        for (int x = 0; x < grid.width; x++) grid.marsh[row * grid.width + x] = true;
        grid.revision++;
    }
}
