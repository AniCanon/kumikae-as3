package dev.kumikae.lanternreach.sim {
    public final class EconomySystem {
        private var town:Buildings;
        private var rules:Rules;
        private var keeper:Keeper;

        public function EconomySystem(buildings:Buildings, defs:Rules, player:Keeper) {
            town = buildings;
            rules = defs;
            keeper = player;
        }

        public function update(tick:int):void {
            var def:StructureDef = rules.structures[Structure.RIG];
            for each (var rig:Structure in town.placed) {
                if (rig.kind != Structure.RIG) continue;
                var period:int = rig.branch == 2 ? int(def.period / 2) : def.period;
                if (tick % period == period - 1) keeper.salvage += def.power * rig.tier + (rig.branch == 1 ? def.power : 0);
            }
        }
    }
}
