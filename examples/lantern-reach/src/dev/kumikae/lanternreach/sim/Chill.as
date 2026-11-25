package dev.kumikae.lanternreach.sim {
    public final class Chill implements IStatusEffect {
        public function apply(enemy:Creature, ticks:int):void {
            enemy.slow = Math.max(enemy.slow, ticks);
        }
    }
}
