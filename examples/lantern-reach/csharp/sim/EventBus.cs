using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class EventBus {
    private List<Subscription> listeners = new List<Subscription>();
    private List<WorldEvent> pending = new List<WorldEvent>();

    public Subscription listen(Action<WorldEvent> callback) {
        Subscription slot = new Subscription();
        slot.notify = callback;
        listeners.Add(slot);
        return slot;
    }

    public void remove(Subscription slot) {
        slot.active = false;
        slot.notify = null;
        int i = listeners.IndexOf(slot);
        if (i >= 0) listeners.RemoveAt(i);
    }

    public void post(int kind, int cell, int value) {
        pending.Add(new WorldEvent(kind, cell, value));
    }

    public void dispatch() {
        if (pending.Count == 0) return;
        List<Subscription> snapshot = new List<Subscription>(listeners);
        foreach (WorldEvent item in pending) {
            foreach (Subscription slot in snapshot) {
                if (slot.active) slot.notify(item);
            }
        }
        pending.Clear();
    }
}
