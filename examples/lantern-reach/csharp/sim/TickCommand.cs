using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class TickCommand {
    public int tick;
    public string action;
    public double x;
    public double y;

    public TickCommand(int at, string verb, double a, double b) {
        tick = at;
        action = verb;
        x = a;
        y = b;
    }
}
