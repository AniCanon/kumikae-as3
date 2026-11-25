package dev.kumikae.lanternreach.sim {
    public final class StatusSystem {
        private var pool:CreaturePool;
        private var index:SpatialIndex;
        private var neighbours:Vector.<Creature> = new Vector.<Creature>();

        public function StatusSystem(creatures:CreaturePool, space:SpatialIndex) {
            pool = creatures;
            index = space;
        }

        public function update(tick:int):void {
            for each (var actor:Creature in pool.slots) {
                if (!actor.active) continue;
                if (actor.slow > 0) actor.slow--;
                if (actor.buff > 0) actor.buff--;
                if (actor.burn == 0) continue;
                actor.burn--;
                if (tick % 20 != 0) continue;
                actor.hurt(2);
                index.nearby(actor.x, actor.y, 0.5, neighbours);
                for each (var other:Creature in neighbours) {
                    if (other.burn == 0 && actor.burn > 20) other.burn = actor.burn - 20;
                }
            }
        }
    }
}
