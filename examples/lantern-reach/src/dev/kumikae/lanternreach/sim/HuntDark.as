package dev.kumikae.lanternreach.sim {
    public final class HuntDark implements IEnemyState {
        public function think(actor:Creature, senses:EnemySenses):void {
            var best:Number = 256;
            var target:Structure = null;
            for each (var tower:Structure in senses.town.placed) {
                if (senses.light.level[tower.cell] > 0) continue;
                var score:Number = senses.distance(actor, tower.cell);
                if (score < best) {
                    best = score;
                    target = tower;
                }
            }
            if (target != null) {
                senses.seek(actor, target.cell);
                senses.strike(actor, target);
            }
        }
    }
}
