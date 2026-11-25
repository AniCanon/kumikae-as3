using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class StatusSystem {
    private CreaturePool pool;
    private SpatialIndex index;
    private List<Creature> neighbours = new List<Creature>();

    public StatusSystem(CreaturePool creatures, SpatialIndex space) {
        pool = creatures;
        index = space;
    }

    public void update(int tick) {
        foreach (Creature actor in pool.slots) {
            if (!actor.active) continue;
            if (actor.slow > 0) actor.slow--;
            if (actor.buff > 0) actor.buff--;
            if (actor.burn == 0) continue;
            actor.burn--;
            if (tick % 20 != 0) continue;
            actor.hurt(2);
            index.nearby(actor.x, actor.y, 0.5, neighbours);
            foreach (Creature other in neighbours) {
                if (other.burn == 0 && actor.burn > 20) other.burn = actor.burn - 20;
            }
        }
    }
}
