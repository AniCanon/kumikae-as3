using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class Movement {
    private TownGrid grid;
    private CreaturePool pool;

    public Movement(TownGrid map, CreaturePool creatures) {
        grid = map;
        pool = creatures;
    }

    public void update() {
        for (int i = 0; i < pool.slots.Count; i++) {
            Creature actor = pool.slots[i];
            if (!actor.active) continue;
            double length = Math.Sqrt(actor.steerX * actor.steerX + actor.steerY * actor.steerY);
            double scale = actor.speed * (grid.marsh[grid.cell(actor.x, actor.y)] && actor.kind != Creature.WISP ? 0.5 : 1)
                * (actor.lit ? 0.8 : 1) * (actor.slow > 0 ? 0.5 : 1) * (actor.buff > 0 ? 1.4 : 1) / Math.Max(1, length);
            actor.vx = actor.steerX * scale;
            actor.vy = actor.steerY * scale;
            double px = Math.Max(0, Math.Min(grid.width - 0.001, actor.x + actor.vx));
            double py = Math.Max(0, Math.Min(grid.height - 0.001, actor.y + actor.vy));
            if (actor.kind == Creature.WISP || !grid.walls[grid.cell(px, actor.y)]) actor.x = px;
            if (actor.kind == Creature.WISP || !grid.walls[grid.cell(actor.x, py)]) actor.y = py;
        }
    }
}
