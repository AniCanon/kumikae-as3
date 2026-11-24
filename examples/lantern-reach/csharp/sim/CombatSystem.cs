using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class CombatSystem {
    public List<int> beaconHealth = new List<int>();
    private TownGrid grid;
    private CreaturePool pool;
    private SpawnSystem spawn;
    private PickupSystem drops;
    private Arsenal arsenal;
    private EventBus events;
    private Rules rules;

    public CombatSystem(TownGrid map, CreaturePool creatures, SpawnSystem spawner,
            PickupSystem pickups, Arsenal weapons, EventBus bus, Rules defs) {
        grid = map;
        pool = creatures;
        spawn = spawner;
        drops = pickups;
        arsenal = weapons;
        events = bus;
        rules = defs;
        for (int i = 0; i < grid.beacons.Count; i++) beaconHealth.Add(120);
    }

    public void update(int tick, List<Hit> hits) {
        foreach (Hit hit in hits) {
            hit.target.hurt(hit.damage);
            events.post(WorldEvent.DAMAGE, grid.cell(hit.target.x, hit.target.y), hit.damage);
        }
        arsenal.update(tick);
        int count = pool.slots.Count;
        for (int i = 0; i < count; i++) {
            Creature actor = pool.slots[i];
            if (!actor.active) continue;
            int cell = grid.cell(actor.x, actor.y);
            if (actor.hp == 0) {
                death(actor, cell);
                continue;
            }
            int beacon = grid.beacons.IndexOf(cell);
            if (beacon < 0 || actor.cooldown > 0) continue;
            EnemyDef def = rules.enemies[actor.kind];
            actor.cooldown = def.period;
            beaconHealth[beacon] -= def.damage;
            events.post(WorldEvent.DAMAGE, cell, def.damage);
            if (beaconHealth[beacon] <= 0) {
                grid.beacons.RemoveAt(beacon);
                beaconHealth.RemoveAt(beacon);
                grid.revision++;
                events.post(WorldEvent.BEACON_LOST, cell, 0);
            }
        }
    }

    private void death(Creature actor, int cell) {
        drops.drop(actor.x, actor.y, rules.enemies[actor.kind].xp);
        events.post(WorldEvent.DEATH, cell, actor.kind);
        if (actor.kind == Creature.SPLITTER && actor.generation < 3) {
            for (int i = 0; i < 2; i++) {
                double x = Math.Max(0, Math.Min(grid.width - 0.001, actor.x + (i == 0 ? -0.25 : 0.25)));
                spawn.place(actor.kind, x, actor.y, actor.generation + 1);
            }
        }
        pool.release(actor);
    }
}
