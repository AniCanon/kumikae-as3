using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class Hit {
    public Creature target;
    public int damage;

    public Hit(Creature enemy, int amount) {
        target = enemy;
        damage = amount;
    }
}
