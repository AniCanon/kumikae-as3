using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class SwarmSteering {
    private TownGrid grid;
    private CreaturePool pool;
    private SpatialIndex index;
    private FlowField flow;

    public SwarmSteering(TownGrid map, CreaturePool creatures, SpatialIndex space, FlowField paths) {
        grid = map;
        pool = creatures;
        index = space;
        flow = paths;
    }

    public void update() {
        for (int i = 0; i < pool.slots.Count; i++) {
            Creature actor = pool.slots[i];
            if (actor.active) steer(actor);
        }
    }

    private void steer(Creature actor) {
        int cx = (int)(actor.x);
        int cy = (int)(actor.y);
        double repelX = 0;
        double repelY = 0;
        double alignX = 0;
        double alignY = 0;
        double centreX = 0;
        double centreY = 0;
        int count = 0;
        for (int y = Math.Max(0, cy - 1); y <= Math.Min(grid.height - 1, cy + 1); y++) {
            for (int x = Math.Max(0, cx - 1); x <= Math.Min(grid.width - 1, cx + 1); x++) {
                int id = index.head[y * grid.width + x];
                while (id >= 0) {
                    Creature near = pool.slots[id];
                    id = near.nextInCell;
                    double dx = actor.x - near.x;
                    double dy = actor.y - near.y;
                    double squared = dx * dx + dy * dy;
                    if (squared == 0 || squared > 1) continue;
                    count++;
                    centreX -= dx;
                    centreY -= dy;
                    alignX += near.vx;
                    alignY += near.vy;
                    if (squared < 0.25) {
                        repelX += dx / (0.04 + squared);
                        repelY += dy / (0.04 + squared);
                    }
                }
            }
        }
        int target = flow.toward[grid.cell(actor.x, actor.y)];
        double tx = target >= 0 ? target % grid.width + 0.5 - actor.x : 0;
        double ty = target >= 0 ? (int)(target / grid.width) + 0.5 - actor.y : 0;
        int divisor = Math.Max(1, count);
        actor.steerX = tx + repelX * 0.25 + (alignX * 0.2 + centreX * 0.1) / divisor;
        actor.steerY = ty + repelY * 0.25 + (alignY * 0.2 + centreY * 0.1) / divisor;
    }
}
