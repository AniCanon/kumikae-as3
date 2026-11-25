using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class FlowField {
    public const int UNREACHABLE = 2147483647;
    public List<int> cost = new List<int>();
    public List<int> toward = new List<int>();
    private TownGrid grid;
    private List<FlowBucket> buckets = new List<FlowBucket>();
    private int builtRevision = -1;

    public FlowField(TownGrid map) {
        grid = map;
        for (int i = 0; i < grid.walls.Count; i++) {
            cost.Add(UNREACHABLE);
            toward.Add(-1);
        }
        for (int j = 0; j < 4; j++) buckets.Add(new FlowBucket());
    }

    public void update() {
        if (builtRevision != grid.revision) rebuild();
    }

    public void rebuild() {
        for (int i = 0; i < cost.Count; i++) {
            cost[i] = UNREACHABLE;
            toward[i] = -1;
        }
        for (int j = 0; j < grid.beacons.Count; j++) {
            int source = grid.beacons[j];
            cost[source] = 0;
            toward[source] = source;
            buckets[0].cells.Add(source);
        }
        propagate(0, grid.beacons.Count);
    }

    // Opening a wall only lowers costs. Seed the four reachable neighbours.
    public void opened(int cellId) {
        int best = UNREACHABLE;
        int parent = -1;
        for (int d = 0; d < 4; d++) {
            int n = grid.neighbour(cellId, d);
            if (n >= 0 && cost[n] < best) {
                parent = n;
                best = cost[n];
            }
        }
        if (parent >= 0) {
            best += grid.marsh[cellId] ? 3 : 1;
            cost[cellId] = best;
            toward[cellId] = parent;
            buckets[best % 4].cells.Add(cellId);
            propagate(best, 1);
        }
    }

    private void propagate(int distance, int pending) {
        while (pending > 0) {
            FlowBucket bucket = buckets[distance % 4];
            for (int i = 0; i < bucket.cells.Count; i++) {
                int from = bucket.cells[i];
                pending--;
                if (cost[from] != distance) continue;
                for (int d = 0; d < 4; d++) {
                    int n = grid.neighbour(from, d);
                    if (n < 0 || grid.walls[n]) continue;
                    int value = distance + (grid.marsh[n] ? 3 : 1);
                    if (value >= cost[n]) continue;
                    cost[n] = value;
                    toward[n] = from;
                    buckets[value % 4].cells.Add(n);
                    pending++;
                }
            }
            bucket.cells.Clear();
            distance++;
        }
        builtRevision = grid.revision;
    }
}
