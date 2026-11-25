using System;
using System.Collections.Generic;
using Godot;

namespace Kumikae.LanternReach.Tests;

public sealed class SystemCases {
    private Simulation game;

    private static void Require(bool value) {
        if (!value) throw new InvalidOperationException("Unexpected simulation state");
    }

    public void run() {
        Action[] cases = { core, lastApproach, fixedClockAndCommands,
            seawardSpawnAndWaves,
            movingAndFlocking,
            neighboursAndReuse,
            collisionThenDamage,
            burningSpreads,
            shardsOfferDistinctChoices,
            occlusionHidesLurkers,
            rigPaysAndSaleRefundsHalf,
            eventsAndRemoval,
            listenerCanPostFollowup, bossTelegraphsBeforeFlood,
            splitAtMostThreeGenerations,
            shoreSalvageSurvivesRestore, lastBeaconEndsWatch };
        foreach (var test in cases) {
            game = new Scenario("station", () => 0).simulation;
            try { test(); GD.Print("PASS ", test.Method.Name); }
            finally { game.stats.close(game.events); }
        }
    }

    private void core() {
        var rng = new RandomStream(1);
        Require(rng.next() == 270369 && rng.next() == 67634689);
        var grid = new TownGrid(5, 3);
        grid.beacons.Add(9);
        grid.walls[7] = true;
        var field = new FlowField(grid);
        field.rebuild();
        Require(field.cost[5] == 6 && field.cost[9] == 0);
    }

    private void lastApproach() {
        var grid = new TownGrid(3, 3);
        grid.beacons.Add(7);
        grid.walls[3] = grid.walls[5] = true;
        var routes = new BuildRoutes(grid);
        Require(!routes.permits(4) && !grid.walls[4] && routes.permits(0));
    }

    private void fixedClockAndCommands() {
        FixedStep clock = new FixedStep();
        Require(clock.advance(0.5 / 60) == 0);
        Require(clock.advance(0.5 / 60) == 1);
        game.commands.enqueue(new TickCommand(1, "move", 1, 0));
        game.step();
        Require(game.keeper.x == 32);
        game.step();
        Require(game.keeper.x > 32);
    }

    private void seawardSpawnAndWaves() {
        Wave wave = new Wave();
        wave.tick = 1;
        wave.count = 4;
        wave.depth = 2;
        wave.name = 1;
        game.spawn.waves.Add(wave);
        game.step();
        Require(game.pool.count == 0);
        game.step();
        Require(game.pool.count == 4 && game.stats.waves == 1);
        foreach (Creature actor in game.pool.slots) Require(actor.y < 2.1);
    }

    private void movingAndFlocking() {
        Creature first = game.spawn.place(Creature.SWARMER, 20.2, 12, 0);
        Creature second = game.spawn.place(Creature.SWARMER, 20.3, 12, 0);
        game.step();
        game.step();
        Require(first.x < second.x && first.y > 12 && first.vx != second.vx);
    }

    private void neighboursAndReuse() {
        Creature near = game.spawn.place(0, 5.5, 4, 0);
        Creature far = game.spawn.place(0, 12, 4, 0);
        SpatialIndex spatial = new SpatialIndex(game.grid, game.pool);
        spatial.rebuild();
        List<Creature> found = new List<Creature>();
        spatial.nearby(5, 4, 1, found);
        Require(found.Count == 1 && found[0] == near);
        game.pool.release(far);
        Require(game.spawn.place(1, 7, 8, 0) == far);
    }

    private void collisionThenDamage() {
        Creature enemy = game.spawn.place(Creature.BRUTE, 8.45, 8, 0);
        game.shots.fire(8, 8, 1, 0, 7, 8, 1);
        game.step();
        Require(enemy.hp == game.rules.enemies[Creature.BRUTE].health - 7);
        Require(!game.shots.shots[0].active);
    }

    private void burningSpreads() {
        Creature first = game.spawn.place(Creature.BRUTE, 4, 3, 0);
        Creature other = game.spawn.place(Creature.BRUTE, 4.1, 3, 0);
        first.burn = 80;
        game.step();
        Require(first.burn == 79 && other.burn > 0 && other.burn < first.burn);
    }

    private void shardsOfferDistinctChoices() {
        game.pickups.drop(game.keeper.x, game.keeper.y, 20);
        game.step();
        Require(game.keeper.level == 2 && game.keeper.offers.Count == 3);
        Require(game.keeper.offers[0] != game.keeper.offers[1] && game.keeper.offers[1] != game.keeper.offers[2]);
        game.commands.enqueue(new TickCommand(1, "choose", 0, 0));
        game.step();
        Require(game.keeper.offers.Count == 0);
    }

    private void occlusionHidesLurkers() {
        game.town.build(10 * 64 + 10, Structure.LAMP);
        game.town.build(10 * 64 + 11, Structure.WALL);
        Creature shadow = game.spawn.place(Creature.LURKER, 12, 10, 0);
        game.light.update();
        int hp = shadow.hp;
        shadow.hurt(7);
        Require(!shadow.lit && shadow.hp == hp);
        game.town.remove(10 * 64 + 11, true);
        game.light.update();
        shadow.hurt(7);
        Require(shadow.lit && shadow.hp == hp - 7);
    }

    private void rigPaysAndSaleRefundsHalf() {
        int cell = 10 * 64 + 10;
        game.town.build(cell, Structure.RIG);
        game.tick = game.rules.structures[Structure.RIG].period - 1;
        int coins = game.keeper.salvage;
        game.step();
        Require(game.keeper.salvage == coins + 3);
        game.town.remove(cell, true);
        Require(game.keeper.salvage == coins + 10);
    }

    private void eventsAndRemoval() {
        RunStats stats = new RunStats(game.events);
        game.events.post(WorldEvent.BUILT, 1, 0);
        Require(stats.built == 0);
        game.events.dispatch();
        stats.close(game.events);
        game.events.post(WorldEvent.BUILT, 2, 0);
        game.events.dispatch();
        Require(stats.built == 1 && game.stats.built == 2);
    }

    private void listenerCanPostFollowup() {
        var listener = game.events.listen(item => {
            if (item.kind == WorldEvent.BUILT) game.events.post(WorldEvent.DESTROYED, item.cell, 0);
        });
        game.events.post(WorldEvent.BUILT, 3, 0);
        game.events.dispatch();
        game.events.dispatch();
        Require(game.stats.destroyed == 1);
        game.events.remove(listener);
    }

    private void bossTelegraphsBeforeFlood() {
        Creature boss = game.spawn.place(Creature.UNDERTOW, 25, 2, 0);
        boss.hp = 1000;
        boss.cooldown = 0;
        game.step();
        Require(boss.warning == 1 && boss.flood == 0);
        for (int i = 0; i < 45; i++) game.step();
        Require(boss.flood == 1 && game.grid.revision > 0);
    }

    private void splitAtMostThreeGenerations() {
        Creature parent = game.spawn.place(Creature.SPLITTER, 12, 4, 2);
        parent.hp = 0;
        game.step();
        Require(game.pool.count == 2);
        foreach (Creature child in game.pool.slots) {
            if (child.active) { Require(child.generation == 3); child.hp = 0; }
        }
        game.step();
        Require(game.pool.count == 0 && game.stats.deaths == 3);
    }

    private void shoreSalvageSurvivesRestore() {
        var tides = new Tides(game);
        tides.morning();
        var before = game.checksum();
        game.wrack[0] = -1;
        Require(before != game.checksum());
        var saved = Dawn.write(game, 1);
        game.wrack.Clear();
        Dawn.read(game, saved);
        Require(game.wrack.Count == 3 && game.wrack[0] == -1);
    }

    private void lastBeaconEndsWatch() {
        Tides tides = new Tides(game);
        tides.morning();
        tides.dusk();
        foreach (int beacon in game.grid.beacons) {
            Creature brute = game.spawn.place(Creature.BRUTE, beacon % 64 + 0.5, (int)(beacon / 64) + 0.5, 0);
            brute.cooldown = 0;
        }
        for (int i = 0; i < game.combat.beaconHealth.Count; i++) game.combat.beaconHealth[i] = 1;
        tides.advance();
        Require(tides.phase == "loss" && game.stats.lost == 3);
    }
}
