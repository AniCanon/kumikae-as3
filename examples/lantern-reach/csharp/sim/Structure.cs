using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class Structure : IDamageable {
    public const int LAMP = 0;
    public const int WALL = 1;
    public const int HARPOON = 2;
    public const int BRAZIER = 3;
    public const int BELL = 4;
    public const int RIG = 5;
    public int cell;
    public int kind;
    public int hp;
    public int tier = 1;
    public int branch;
    public int spent;
    public int cooldown;
    public ITargeting policy;

    public void hurt(int amount) {
        hp = Math.Max(0, hp - amount);
    }
}
