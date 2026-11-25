package dev.kumikae.lanternreach.sim {
    public final class CollisionSystem {
        public var hits:Vector.<Hit> = new Vector.<Hit>();
        private var index:SpatialIndex;
        private var pool:ShotPool;
        private var nearby:Vector.<Creature> = new Vector.<Creature>();

        public function CollisionSystem(space:SpatialIndex, shots:ShotPool) {
            index = space;
            pool = shots;
        }

        public function update():void {
            hits.length = 0;
            for each (var shot:Shot in pool.shots) {
                if (!shot.active) continue;
                shot.x += shot.dx * 0.45;
                shot.y += shot.dy * 0.45;
                shot.left -= 0.45;
                index.nearby(shot.x, shot.y, 0.48, nearby);
                for each (var enemy:Creature in nearby) {
                    if (shot.hit.indexOf(enemy.id) >= 0 || (enemy.kind == Creature.WISP && enemy.warning > 0 && enemy.warning <= 6)) continue;
                    if (enemy.kind == Creature.LURKER && !enemy.lit) continue;
                    hits.push(new Hit(enemy, shot.damage));
                    shot.hit.push(enemy.id);
                    shot.remaining--;
                    if (shot.remaining == 0) break;
                }
                if (shot.left <= 0 || shot.remaining == 0) pool.release(shot);
            }
        }
    }
}
