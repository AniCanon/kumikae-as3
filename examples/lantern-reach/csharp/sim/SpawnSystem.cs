using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class SpawnSystem {
    private TownGrid grid;
    private CreaturePool pool;
    private RandomStream random;
    private int remaining;

    public SpawnSystem(TownGrid map, CreaturePool creatures, RandomStream stream, int population) {
        grid = map;
        pool = creatures;
        random = stream;
        remaining = population;
    }

    public void update() {
        while (remaining > 0) {
            double x = random.below(grid.width * 1024) / 1024.0;
            double y = random.below(5 * 1024) / 1024.0;
            pool.spawn(x, y);
            remaining--;
        }
    }
}
