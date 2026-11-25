using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class FirstAlongPath : ITargeting {
    public double score(Creature enemy, double distance, int pathCost) {
        return pathCost;
    }
}
