using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class CreaturePool {
    public List<Creature> slots = new List<Creature>();
    private List<int> free = new List<int>();
    public int count;
    public int peak;

    public Creature spawn(double x, double y) {
        int slot = slots.Count;
        if (free.Count > 0) {
            slot = free[^1];
            free.RemoveAt(free.Count - 1);
        } else {
            slots.Add(new Creature());
        }
        Creature creature = slots[slot];
        creature.reset(slot, x, y);
        count++;
        if (count > peak) peak = count;
        return creature;
    }

    public void release(Creature creature) {
        if (!creature.active) return;
        creature.active = false;
        free.Add(creature.id);
        count--;
    }

    public void clear() {
        slots.Clear();
        free.Clear();
        count = 0;
    }
}
