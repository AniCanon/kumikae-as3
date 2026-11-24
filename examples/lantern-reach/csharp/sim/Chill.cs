using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class Chill : IStatusEffect {
    public void apply(Creature enemy, int ticks) {
        enemy.slow = Math.Max(enemy.slow, ticks);
    }
}
