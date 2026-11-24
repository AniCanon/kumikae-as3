namespace Kumikae.LanternReach;

public interface IEnemyState
{
    void think(Creature actor, EnemySenses senses);
}
