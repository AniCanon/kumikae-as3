using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class EconomySystem {
    private Buildings town;
    private Rules rules;
    private Keeper keeper;

    public EconomySystem(Buildings buildings, Rules defs, Keeper player) {
        town = buildings;
        rules = defs;
        keeper = player;
    }

    public void update(int tick) {
        StructureDef def = rules.structures[Structure.RIG];
        foreach (Structure rig in town.placed) {
            if (rig.kind != Structure.RIG) continue;
            int period = rig.branch == 2 ? (int)(def.period / 2) : def.period;
            if (tick % period == period - 1) keeper.salvage += def.power * rig.tier + (rig.branch == 1 ? def.power : 0);
        }
    }
}
