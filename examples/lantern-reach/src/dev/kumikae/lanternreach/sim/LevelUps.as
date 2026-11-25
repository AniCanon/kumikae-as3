package dev.kumikae.lanternreach.sim {
    public final class LevelUps {
        private var player:Keeper;
        private var rules:Rules;
        private var random:RandomStream;
        private var events:EventBus;

        public function LevelUps(keeper:Keeper, defs:Rules, stream:RandomStream, bus:EventBus) {
            player = keeper;
            rules = defs;
            random = stream;
            events = bus;
        }

        public function offer():void {
            var threshold:int = 6 + player.level * 4;
            if (player.offers.length > 0 || player.xp < threshold) return;
            player.xp -= threshold;
            player.level++;
            for (var pick:int = 0; pick < 3; pick++) {
                var total:int = 0;
                for (var i:int = 0; i < rules.upgrades.length; i++) {
                    if (player.offers.indexOf(i) < 0) total += rules.upgrades[i].weight;
                }
                if (total == 0) break;
                var roll:int = random.below(total);
                for (var j:int = 0; j < rules.upgrades.length; j++) {
                    if (player.offers.indexOf(j) >= 0) continue;
                    roll -= rules.upgrades[j].weight;
                    if (roll < 0) {
                        player.offers.push(j);
                        break;
                    }
                }
            }
            events.post(WorldEvent.LEVEL, 0, player.level);
        }

        public function choose(choice:int):void {
            if (choice < 0 || choice >= player.offers.length) return;
            var def:UpgradeDef = rules.upgrades[player.offers[choice]];
            if (def.stat == "damage") player.damage += int(def.amount);
            if (def.stat == "speed") player.speed += def.amount;
            if (def.stat == "pull") player.pull += def.amount;
            if (def.stat == "range") player.range += int(def.amount);
            if (def.stat == "haste") player.haste += int(def.amount);
            if (def.stat == "spark") player.spark += int(def.amount);
            if (def.stat == "orbit") player.orbit += int(def.amount);
            player.offers.length = 0;
        }
    }
}
