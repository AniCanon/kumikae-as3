using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class CollisionSystem {
    public List<Hit> hits = new List<Hit>();
    private SpatialIndex index;
    private ShotPool pool;
    private List<Creature> nearby = new List<Creature>();

    public CollisionSystem(SpatialIndex space, ShotPool shots) {
        index = space;
        pool = shots;
    }

    public void update() {
        hits.Clear();
        foreach (Shot shot in pool.shots) {
            if (!shot.active) continue;
            shot.x += shot.dx * 0.45;
            shot.y += shot.dy * 0.45;
            shot.left -= 0.45;
            index.nearby(shot.x, shot.y, 0.48, nearby);
            foreach (Creature enemy in nearby) {
                if (shot.hit.IndexOf(enemy.id) >= 0 || (enemy.kind == Creature.WISP && enemy.warning > 0 && enemy.warning <= 6)) continue;
                if (enemy.kind == Creature.LURKER && !enemy.lit) continue;
                hits.Add(new Hit(enemy, shot.damage));
                shot.hit.Add(enemy.id);
                shot.remaining--;
                if (shot.remaining == 0) break;
            }
            if (shot.left <= 0 || shot.remaining == 0) pool.release(shot);
        }
    }
}
