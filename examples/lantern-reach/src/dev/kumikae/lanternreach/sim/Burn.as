package dev.kumikae.lanternreach.sim {
    public final class Burn implements IStatusEffect {
        public function apply(enemy:Creature, ticks:int):void {
            enemy.burn = Math.max(enemy.burn, ticks);
        }
    }
}
