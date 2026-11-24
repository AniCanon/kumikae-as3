package dev.kumikae.lanternreach.sim {
    public final class FirstAlongPath implements ITargeting {
        public function score(enemy:Creature, distance:Number, pathCost:int):Number {
            return pathCost;
        }
    }
}
