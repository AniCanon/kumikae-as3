using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class Strongest : ITargeting {
    public double score(Creature enemy, double distance, int pathCost) {
        return -enemy.hp;
    }
}
