using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class UndertowState : IEnemyState {
    private List<IEnemyState> phases = new List<IEnemyState>();

    public UndertowState() {
        phases.Add(new ChainSweep());
        phases.Add(new FloodMarsh());
        phases.Add(new WakeWrecks());
    }

    public void think(Creature actor, EnemySenses senses) {
        EnemyDef def = senses.rules.enemies[actor.kind];
        actor.phase = actor.hp * 3 > def.health * 2 ? 0 : actor.hp * 3 > def.health ? 1 : 2;
        if (actor.cooldown > 0) return;
        if (actor.warning == 0) {
            actor.warning = 1;
            actor.cooldown = 45;
            return;
        }
        actor.warning = 0;
        actor.cooldown = (int)(def.period / (actor.phase + 1));
        for (int i = 0; i <= actor.phase; i++) phases[i].think(actor, senses);
    }
}
