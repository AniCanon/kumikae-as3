using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class Burn : IStatusEffect {
    public void apply(Creature enemy, int ticks) {
        enemy.burn = Math.Max(enemy.burn, ticks);
    }
}
