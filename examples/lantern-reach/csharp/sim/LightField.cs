using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class LightField {
    public List<int> level = new List<int>();
    public int daylight;
    private TownGrid grid;
    private Buildings buildings;
    private CreaturePool pool;
    private Rules rules;

    public LightField(TownGrid map, Buildings town, CreaturePool creatures, Rules definitions) {
        grid = map;
        buildings = town;
        pool = creatures;
        rules = definitions;
        for (int i = 0; i < grid.walls.Count; i++) level.Add(0);
    }

    public void update() {
        for (int i = 0; i < level.Count; i++) level[i] = daylight;
        foreach (int id in grid.beacons) shine(id, 5);
        foreach (Structure tower in buildings.placed) {
            if (tower.kind == Structure.LAMP || tower.kind == Structure.BRAZIER) {
                shine(tower.cell, rules.structures[tower.kind].radius + tower.tier + (tower.branch == 1 ? 2 : 0));
            }
        }
        foreach (Creature fire in pool.slots) {
            if (fire.active && fire.burn > 0) shine(grid.cell(fire.x, fire.y), 1);
        }
        foreach (Creature enemy in pool.slots) {
            if (enemy.active) enemy.lit = level[grid.cell(enemy.x, enemy.y)] > 0;
        }
    }

    private void shine(int origin, int radius) {
        int cx = origin % grid.width;
        int cy = (int)(origin / grid.width);
        for (int y = Math.Max(0, cy - radius); y <= Math.Min(grid.height - 1, cy + radius); y++) {
            for (int x = Math.Max(0, cx - radius); x <= Math.Min(grid.width - 1, cx + radius); x++) {
                int dx = x - cx;
                int dy = y - cy;
                int squared = dx * dx + dy * dy;
                if (squared > radius * radius) continue;
                int steps = Math.Max(Math.Abs(dx), Math.Abs(dy));
                bool clear = true;
                for (int j = 1; j < steps; j++) {
                    if (grid.walls[grid.cell(cx + 0.5 + dx * j / (double)(steps), cy + 0.5 + dy * j / (double)(steps))]) {
                        clear = false;
                        break;
                    }
                }
                int tile = y * grid.width + x;
                if (clear) level[tile] = Math.Max(level[tile], 255 - (int)(220 * squared / (radius * radius)));
            }
        }
    }
}
