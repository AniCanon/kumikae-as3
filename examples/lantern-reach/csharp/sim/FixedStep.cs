using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class FixedStep {
    private double accumulated = 0;

    public int advance(double delta) {
        accumulated += delta * 60;
        int ticks = (int)(accumulated);
        accumulated -= ticks;
        return ticks;
    }
}
