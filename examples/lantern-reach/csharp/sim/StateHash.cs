using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class StateHash {
    private uint high = 0xcbf29ce4;
    private uint low = 0x84222325;

    // FNV-1a's prime is 2^40 + 435. Both partial products fit exactly in a double.
    public void word(uint value) {
        for (int i = 0; i < 4; i++) {
            low ^= value & 255;
            double product = (double)(low) * 435;
            double upper = (double)(high) * 435 + (double)(low) * 256 + Math.Floor(product / 4294967296.0);
            high = (uint)(upper % 4294967296.0);
            low = (uint)(product % 4294967296.0);
            value = value >> 8;
        }
    }

    public void coordinate(double value) {
        word((uint)((int)(Math.Floor(value * 65536))));
    }

    private string hex(uint value) {
        string digits = "0123456789abcdef";
        string result = "";
        for (int i = 0; i < 8; i++) {
            result = digits[(int)(value & 15)] + result;
            value = value >> 4;
        }
        return result;
    }

    public string finish() {
        return hex(high) + hex(low);
    }
}
