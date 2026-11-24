package dev.kumikae.lanternreach.sim {
    public final class Nearest implements ITargeting {
        public function score(enemy:Creature, distance:Number, pathCost:int):Number {
            return distance;
        }
    }
}
