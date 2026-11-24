using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class LevelUps {
    private Keeper player;
    private Rules rules;
    private RandomStream random;
    private EventBus events;

    public LevelUps(Keeper keeper, Rules defs, RandomStream stream, EventBus bus) {
        player = keeper;
        rules = defs;
        random = stream;
        events = bus;
    }

    public void offer() {
        int threshold = 6 + player.level * 4;
        if (player.offers.Count > 0 || player.xp < threshold) return;
        player.xp -= threshold;
        player.level++;
        for (int pick = 0; pick < 3; pick++) {
            int total = 0;
            for (int i = 0; i < rules.upgrades.Count; i++) {
                if (player.offers.IndexOf(i) < 0) total += rules.upgrades[i].weight;
            }
            if (total == 0) break;
            int roll = random.below(total);
            for (int j = 0; j < rules.upgrades.Count; j++) {
                if (player.offers.IndexOf(j) >= 0) continue;
                roll -= rules.upgrades[j].weight;
                if (roll < 0) {
                    player.offers.Add(j);
                    break;
                }
            }
        }
        events.post(WorldEvent.LEVEL, 0, player.level);
    }

    public void choose(int choice) {
        if (choice < 0 || choice >= player.offers.Count) return;
        UpgradeDef def = rules.upgrades[player.offers[choice]];
        if (def.stat == "damage") player.damage += (int)(def.amount);
        if (def.stat == "speed") player.speed += def.amount;
        if (def.stat == "pull") player.pull += def.amount;
        if (def.stat == "range") player.range += (int)(def.amount);
        if (def.stat == "haste") player.haste += (int)(def.amount);
        if (def.stat == "spark") player.spark += (int)(def.amount);
        if (def.stat == "orbit") player.orbit += (int)(def.amount);
        player.offers.Clear();
    }
}
