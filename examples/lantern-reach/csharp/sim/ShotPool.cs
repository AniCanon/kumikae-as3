using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class ShotPool {
    public List<Shot> shots = new List<Shot>();
    private List<Shot> free = new List<Shot>();

    public void fire(double x, double y, double dx, double dy, int damage, int reach, int pierce) {
        double length = Math.Sqrt(dx * dx + dy * dy);
        if (length == 0) return;
        Shot shot;
        if (free.Count == 0) {
            shot = new Shot();
            shots.Add(shot);
        } else {
            shot = free[^1];
            free.RemoveAt(free.Count - 1);
        }
        shot.x = x;
        shot.y = y;
        shot.dx = dx / length;
        shot.dy = dy / length;
        shot.left = reach;
        shot.damage = damage;
        shot.remaining = pierce;
        shot.active = true;
        shot.hit.Clear();
    }

    public void release(Shot shot) {
        shot.active = false;
        free.Add(shot);
    }

    public void clear() {
        shots.Clear();
        free.Clear();
    }
}
