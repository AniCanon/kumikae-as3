package dev.kumikae.lanternreach.sim {
    public final class Hit {
        public var target:Creature;
        public var damage:int;

        public function Hit(enemy:Creature, amount:int) {
            target = enemy;
            damage = amount;
        }
    }
}
