package dev.kumikae.lanternreach.sim {
    public interface ITargeting {
        function score(enemy:Creature, distance:Number, pathCost:int):Number;
    }
}
