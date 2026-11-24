using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class BreakWall : IEnemyState {
    public void think(Creature actor, EnemySenses senses) {
        int beacon = senses.beacon(actor);
        if (beacon < 0) return;
        double dx = beacon % senses.grid.width + 0.5 - actor.x;
        double dy = (int)(beacon / senses.grid.width) + 0.5 - actor.y;
        double scale = Math.Max(1, Math.Max(Math.Abs(dx), Math.Abs(dy)));
        for (int step = 1; step <= 4; step++) {
            int id = senses.grid.cell(actor.x + dx * step / scale, actor.y + dy * step / scale);
            if (senses.grid.walls[id]) {
                senses.seek(actor, id);
                senses.strike(actor, senses.town.at[id]);
                return;
            }
        }
    }
}
