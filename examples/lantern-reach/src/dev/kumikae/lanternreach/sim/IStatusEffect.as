package dev.kumikae.lanternreach.sim {
    public interface IStatusEffect {
        function apply(enemy:Creature, ticks:int):void;
    }
}
