package dev.kumikae.lanternreach.sim {
    public final class Strongest implements ITargeting {
        public function score(enemy:Creature, distance:Number, pathCost:int):Number {
            return -enemy.hp;
        }
    }
}
