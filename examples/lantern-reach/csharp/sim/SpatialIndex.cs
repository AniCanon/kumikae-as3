using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class SpatialIndex {
    private TownGrid grid;
    private CreaturePool pool;
    public List<int> head = new List<int>();

    public SpatialIndex(TownGrid map, CreaturePool creatures) {
        grid = map;
        pool = creatures;
        for (int i = 0; i < grid.walls.Count; i++) head.Add(-1);
    }

    public void nearby(double px, double py, double radius, List<Creature> result) {
        result.Clear();
        int top = (int)(Math.Max(0, py - radius));
        int bottom = (int)(Math.Min(grid.height - 1, py + radius));
        int left = (int)(Math.Max(0, px - radius));
        int right = (int)(Math.Min(grid.width - 1, px + radius));
        for (int y = top; y <= bottom; y++) {
            for (int x = left; x <= right; x++) {
                int id = head[y * grid.width + x];
                while (id >= 0) {
                    Creature enemy = pool.slots[id];
                    id = enemy.nextInCell;
                    double dx = px - enemy.x;
                    double dy = py - enemy.y;
                    if (enemy.active && dx * dx + dy * dy <= radius * radius) result.Add(enemy);
                }
            }
        }
    }

    public void rebuild() {
        for (int i = 0; i < head.Count; i++) head[i] = -1;
        for (int slot = pool.slots.Count - 1; slot >= 0; slot--) {
            Creature actor = pool.slots[slot];
            if (!actor.active) continue;
            int cellId = grid.cell(actor.x, actor.y);
            actor.nextInCell = head[cellId];
            head[cellId] = slot;
        }
    }
}
