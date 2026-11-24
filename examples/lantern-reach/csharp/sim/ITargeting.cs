namespace Kumikae.LanternReach;

public interface ITargeting
{
    double score(Creature enemy, double distance, int pathCost);
}
