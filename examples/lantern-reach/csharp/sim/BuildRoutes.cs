using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class BuildRoutes {
    private TownGrid grid;
    private List<int> visited = new List<int>();
    private List<int> frontier = new List<int>();
    private int generation;

    public BuildRoutes(TownGrid map) {
        grid = map;
        for (int i = 0; i < map.walls.Count; i++) visited.Add(0);
    }

    public bool permits(int block) {
        generation++;
        frontier.Clear();
        for (int x = 0; x < grid.width; x++) visit(x, block);
        for (int i = 0; i < frontier.Count; i++) {
            for (int d = 0; d < 4; d++) visit(grid.neighbour(frontier[i], d), block);
        }
        for (int b = 0; b < grid.beacons.Count; b++) {
            if (visited[grid.beacons[b]] != generation) return false;
        }
        return true;
    }

    private void visit(int id, int block) {
        if (id < 0 || id == block || grid.walls[id] || visited[id] == generation) return;
        visited[id] = generation;
        frontier.Add(id);
    }
}
