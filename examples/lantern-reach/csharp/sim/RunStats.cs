using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class RunStats {
    public int built;
    public int destroyed;
    public int deaths;
    public int waves;
    public int lost;
    private Subscription subscription;

    public RunStats(EventBus bus) {
        subscription = bus.listen(receive);
    }

    private void receive(WorldEvent item) {
        if (item.kind == WorldEvent.BUILT) built++;
        if (item.kind == WorldEvent.DESTROYED) destroyed++;
        if (item.kind == WorldEvent.DEATH) deaths++;
        if (item.kind == WorldEvent.WAVE_START) waves++;
        if (item.kind == WorldEvent.BEACON_LOST) lost++;
    }

    public void close(EventBus bus) {
        bus.remove(subscription);
    }
}
