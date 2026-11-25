package dev.kumikae.lanternreach.sim {
    public interface IEnemyState {
        function think(actor:Creature, senses:EnemySenses):void;
    }
}
