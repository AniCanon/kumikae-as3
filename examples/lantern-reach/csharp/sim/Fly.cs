using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class Fly : IEnemyState {
    public void think(Creature actor, EnemySenses senses) {
        int target = senses.beacon(actor);
        if (target >= 0) senses.seek(actor, target);
        if (actor.warning > 0) {
            actor.warning--;
            if (actor.warning <= 6) {
                double dx = actor.steerX;
                actor.steerX = actor.steerY * 2;
                actor.steerY = -dx * 2;
            }
        }
    }
}
