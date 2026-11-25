using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class Shot {
    public bool active;
    public double x;
    public double y;
    public double dx;
    public double dy;
    public double left;
    public int damage;
    public int remaining;
    public List<int> hit = new List<int>();
}
