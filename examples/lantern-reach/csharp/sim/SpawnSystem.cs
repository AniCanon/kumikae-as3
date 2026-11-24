using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class SpawnSystem {
    public List<Wave> waves = new List<Wave>();
    public int cursor;
    private TownGrid grid;
    private CreaturePool pool;
    private RandomStream random;
    private Rules rules;
    private EventBus events;

    public SpawnSystem(TownGrid map, CreaturePool creatures, RandomStream stream, Rules defs, EventBus bus) {
        grid = map;
        pool = creatures;
        random = stream;
        rules = defs;
        events = bus;
    }

    public Creature place(int kind, double x, double y, int generation) {
        Creature enemy = pool.spawn(x, y);
        EnemyDef def = rules.enemies[kind];
        enemy.kind = kind;
        enemy.generation = generation;
        enemy.hp = Math.Max(1, (int)(def.health / (1 + generation)));
        enemy.speed = def.speed * (1 + generation * 0.15);
        return enemy;
    }

    public void update(int tick) {
        while (cursor < waves.Count && waves[cursor].tick <= tick) {
            Wave wave = waves[cursor++];
            if (wave.name > 0) {
                if (wave.name > 1) events.post(WorldEvent.WAVE_END, 0, wave.name - 1);
                events.post(WorldEvent.WAVE_START, 0, wave.name);
            }
            for (int i = 0; i < wave.count; i++) {
                double x = random.below(grid.width * 1024) / 1024.0;
                double y = random.below(wave.depth * 1024) / 1024.0;
                place(wave.kind, x, y, 0);
            }
        }
    }
}
