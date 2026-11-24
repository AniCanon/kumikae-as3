package dev.kumikae.lanternreach.sim {
    public final class Fly implements IEnemyState {
        public function think(actor:Creature, senses:EnemySenses):void {
            var target:int = senses.beacon(actor);
            if (target >= 0) senses.seek(actor, target);
            if (actor.warning > 0) {
                actor.warning--;
                if (actor.warning <= 6) {
                    var dx:Number = actor.steerX;
                    actor.steerX = actor.steerY * 2;
                    actor.steerY = -dx * 2;
                }
            }
        }
    }
}
