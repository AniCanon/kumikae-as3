using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class Creature {
    public int id;
    public bool active;
    public double x;
    public double y;
    public double vx;
    public double vy;
    public double steerX;
    public double steerY;
    public int hp;
    public int nextInCell;

    public void reset(int slot, double px, double py) {
        id = slot;
        x = px;
        y = py;
        vx = 0;
        vy = 0;
        steerX = 0;
        steerY = 0;
        hp = 10;
        nextInCell = -1;
        active = true;
    }
}
