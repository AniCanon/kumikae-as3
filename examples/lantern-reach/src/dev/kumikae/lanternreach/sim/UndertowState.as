package dev.kumikae.lanternreach.sim {
    public final class UndertowState implements IEnemyState {
        private var phases:Vector.<IEnemyState> = new Vector.<IEnemyState>();

        public function UndertowState() {
            phases.push(new ChainSweep());
            phases.push(new FloodMarsh());
            phases.push(new WakeWrecks());
        }

        public function think(actor:Creature, senses:EnemySenses):void {
            var def:EnemyDef = senses.rules.enemies[actor.kind];
            actor.phase = actor.hp * 3 > def.health * 2 ? 0 : actor.hp * 3 > def.health ? 1 : 2;
            if (actor.cooldown > 0) return;
            if (actor.warning == 0) {
                actor.warning = 1;
                actor.cooldown = 45;
                return;
            }
            actor.warning = 0;
            actor.cooldown = int(def.period / (actor.phase + 1));
            for (var i:int = 0; i <= actor.phase; i++) phases[i].think(actor, senses);
        }
    }
}
