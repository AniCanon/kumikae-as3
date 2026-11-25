using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class SeekBeacon : IEnemyState {
    public void think(Creature actor, EnemySenses senses) {
        int tile = senses.grid.cell(actor.x, actor.y);
        senses.strike(actor, senses.town.at[tile]);
    }
}
