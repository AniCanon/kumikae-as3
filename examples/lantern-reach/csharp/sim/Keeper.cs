using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class Keeper : IMover {
    public double aimX = 1;
    public double aimY = 0;
    public bool firing;
    public int dash;
    public int recharge;
    public double speed = 1;
    public int lastShot;
    public int damage;
    public int range;
    public double pull = 2.5;
    public int haste;
    public int orbit;
    public int spark;
    public int xp;
    public int level = 1;
    public List<int> offers = new List<int>();
    public double x = 32;
    public double y = 20;
    public double dx = 0;
    public double dy = 0;
    public int salvage = 120;

    public void move(TownGrid grid) {
        if (recharge > 0) recharge--;
        if (dash > 0) dash--;
        double stride = speed * (dash > 0 ? 0.32 : 0.08) * (grid.marsh[grid.cell(x, y)] ? 0.6 : 1);
        double length = Math.Max(1, Math.Sqrt(dx * dx + dy * dy));
        double px = Math.Max(0, Math.Min(grid.width - 0.001, x + dx * stride / length));
        double py = Math.Max(0, Math.Min(grid.height - 0.001, y + dy * stride / length));
        if (!grid.walls[grid.cell(px, y)]) x = px;
        if (!grid.walls[grid.cell(x, py)]) y = py;
    }
}
