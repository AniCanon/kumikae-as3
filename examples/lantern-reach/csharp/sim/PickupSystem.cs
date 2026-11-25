using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class PickupSystem {
    public List<Shard> shards = new List<Shard>();
    private List<int> head = new List<int>();
    private List<int> free = new List<int>();
    private TownGrid grid;
    private Keeper player;
    private LevelUps upgrades;

    public PickupSystem(TownGrid map, Keeper keeper, LevelUps levels) {
        grid = map;
        player = keeper;
        upgrades = levels;
        for (int i = 0; i < map.walls.Count; i++) head.Add(-1);
    }

    public void drop(double x, double y, int value) {
        int id = shards.Count;
        if (free.Count > 0) {
            id = free[^1];
            free.RemoveAt(free.Count - 1);
        } else {
            shards.Add(new Shard());
        }
        Shard shard = shards[id];
        shard.x = x;
        shard.y = y;
        shard.value = value;
        insert(id);
    }

    private void insert(int id) {
        Shard shard = shards[id];
        shard.cell = grid.cell(shard.x, shard.y);
        shard.next = head[shard.cell];
        head[shard.cell] = id;
    }

    public void update() {
        double radius = player.pull;
        List<int> moved = new List<int>();
        for (int y = (int)(Math.Max(0, player.y - radius)); y <= Math.Min(grid.height - 1, player.y + radius); y++) {
            for (int x = (int)(Math.Max(0, player.x - radius)); x <= Math.Min(grid.width - 1, player.x + radius); x++) {
                int cell = y * grid.width + x;
                int prev = -1;
                int id = head[cell];
                while (id >= 0) {
                    Shard shard = shards[id];
                    int next = shard.next;
                    double dx = player.x - shard.x;
                    double dy = player.y - shard.y;
                    double d = dx * dx + dy * dy;
                    if (d < 0.25) {
                        player.xp += shard.value;
                        player.salvage++;
                        shard.value = 0;
                        free.Add(id);
                    } else if (d <= radius * radius) {
                        shard.x += dx * 0.12;
                        shard.y += dy * 0.12;
                    }
                    if (shard.value == 0 || grid.cell(shard.x, shard.y) != cell) {
                        if (prev < 0) head[cell] = next;
                        else shards[prev].next = next;
                        if (shard.value > 0) moved.Add(id);
                    } else prev = id;
                    id = next;
                }
            }
        }
        foreach (int changed in moved) insert(changed);
        upgrades.offer();
    }

    public void clear() {
        shards.Clear();
        free.Clear();
        for (int i = 0; i < head.Count; i++) head[i] = -1;
    }
}
