using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class SpatialIndex {
    private TownGrid grid;
    private CreaturePool pool;
    public List<int> head = new List<int>();

    public SpatialIndex(TownGrid map, CreaturePool creatures) {
        grid = map;
        pool = creatures;
        for (int i = 0; i < grid.walls.Count; i++) head.Add(-1);
    }

    public void rebuild() {
        for (int i = 0; i < head.Count; i++) head[i] = -1;
        for (int slot = pool.slots.Count - 1; slot >= 0; slot--) {
            Creature actor = pool.slots[slot];
            if (!actor.active) continue;
            int cellId = grid.cell(actor.x, actor.y);
            actor.nextInCell = head[cellId];
            head[cellId] = slot;
        }
    }
}
