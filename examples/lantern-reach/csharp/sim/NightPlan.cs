using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class NightPlan {
    public int duration;
    public List<int> shore = new List<int>();
    public List<Wave> waves = new List<Wave>();
}
