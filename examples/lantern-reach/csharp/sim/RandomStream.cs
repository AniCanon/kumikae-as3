using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class RandomStream {
    public uint state;

    public RandomStream(uint seed) {
        state = seed == 0 ? 1 : seed;
    }

    public uint next() {
        uint x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }

    public int below(int limit) {
        return (int)(next() % (uint)(limit));
    }
}
