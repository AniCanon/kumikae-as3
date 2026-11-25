using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class Tides {
    public int number = 1;
    public string phase = "day";
    public int ends;
    private Simulation game;

    public Tides(Simulation sim) { game = sim; }

    public void morning() {
        phase = "day";
        game.light.daylight = 160;
        game.wrack.Clear();
        foreach (int cell in game.rules.nights[number - 1].shore) game.wrack.Add(cell);
    }

    public void dusk() {
        NightPlan plan = game.rules.nights[number - 1];
        foreach (Wave group in plan.waves) {
            Wave wave = new Wave();
            wave.tick = game.tick + group.tick;
            wave.kind = group.kind;
            wave.count = group.count;
            wave.depth = group.depth;
            wave.name = group.name;
            game.spawn.waves.Add(wave);
        }
        ends = game.tick + plan.duration;
        phase = "night";
        game.wrack.Clear();
    }

    public void daylight() {
        game.daylight();
        for (int i = game.wrack.Count - 1; i >= 0; i--) {
            int cell = game.wrack[i];
            if (cell < 0) continue;
            double dx = game.keeper.x - cell % game.grid.width - 0.5;
            double dy = game.keeper.y - (int)(cell / game.grid.width) - 0.5;
            if (dx * dx + dy * dy < 2.25) {
                game.keeper.salvage += 12;
                game.wrack[i] = -1;
            }
        }
    }

    public void advance() {
        int left = ends - game.tick;
        int age = game.rules.nights[number - 1].duration - left;
        game.light.daylight = Math.Max(0, 160 - age);
        game.step();
        if (game.grid.beacons.Count == 0) {
            phase = "loss";
            game.events.post(WorldEvent.LOST_WATCH, 0, number);
        } else if (game.tick >= ends) {
            if (number == 5) {
                foreach (Creature actor in game.pool.slots) {
                    if (actor.active && actor.kind == Creature.UNDERTOW) return;
                }
            }
            game.events.post(WorldEvent.WAVE_END, 0, game.stats.waves);
            game.events.post(WorldEvent.DAWN, 0, number);
            game.clearWatch();
            if (number == 5) phase = "win";
            else {
                number++;
                morning();
            }
        }
        game.events.dispatch();
    }
}
