using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class Keeper {
    public double x = 32;
    public double y = 20;
    public double dx = 0;
    public double dy = 0;
    public int salvage = 120;

    public void move(TownGrid grid) {
        double px = Math.Max(0, Math.Min(grid.width - 0.001, x + dx * 0.08));
        double py = Math.Max(0, Math.Min(grid.height - 0.001, y + dy * 0.08));
        if (!grid.walls[grid.cell(px, y)]) x = px;
        if (!grid.walls[grid.cell(x, py)]) y = py;
    }
}
