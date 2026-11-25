using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class WorldEvent {
    public const int DEATH = 0;
    public const int DAMAGE = 1;
    public const int LEVEL = 2;
    public const int WAVE_START = 3;
    public const int WAVE_END = 4;
    public const int BEACON_LOST = 5;
    public const int BUILT = 6;
    public const int DESTROYED = 7;
    public const int SWING = 10;
    public const int FIRED = 9;
    public const int REFUSED = 8;
    public int kind;
    public int cell;
    public int value;

    public WorldEvent(int type, int tile, int amount) {
        kind = type;
        cell = tile;
        value = amount;
    }

    public const int DAWN = 11;
    public const int LOST_WATCH = 12;
}
