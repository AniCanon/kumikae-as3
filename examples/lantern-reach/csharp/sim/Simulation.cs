using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class Simulation {
    public TownGrid grid;
    public Rules rules;
    public FlowField flow;
    public RandomStream random;
    public Keeper keeper = new Keeper();
    public CreaturePool pool = new CreaturePool();
    public CommandQueue commands = new CommandQueue();
    public EventBus events = new EventBus();
    public RunStats stats;
    public ShotPool shots = new ShotPool();
    public Buildings town;
    public SpawnSystem spawn;
    public LightField light;
    public PickupSystem pickups;
    public CombatSystem combat;
    public int tick;
    public List<double> elapsed = new List<double>();
    private InputSystem input;
    private SpatialIndex index;
    private SwarmSteering ai;
    private Movement movement;
    private CollisionSystem collision;
    private StatusSystem status;
    private EconomySystem economy;
    private Func<double> clock;
    private double previous;

    public Simulation(TownGrid map, uint seed, Rules defs, Func<double> timer) {
        grid = map;
        rules = defs;
        stats = new RunStats(events);
        random = new RandomStream(seed);
        flow = new FlowField(grid);
        index = new SpatialIndex(grid, pool);
        town = new Buildings(grid, rules, keeper, flow, events);
        light = new LightField(grid, town, pool, rules);
        spawn = new SpawnSystem(grid, pool, random, rules, events);
        EnemySenses senses = new EnemySenses(grid, town, index, light, spawn, rules, keeper);
        ai = new SwarmSteering(grid, pool, index, flow, senses);
        movement = new Movement(grid, pool);
        LevelUps levels = new LevelUps(keeper, rules, random, events);
        input = new InputSystem(grid, keeper, commands, town, levels, rules);
        pickups = new PickupSystem(grid, keeper, levels);
        collision = new CollisionSystem(index, shots);
        Arsenal arsenal = new Arsenal(grid, town, index, flow, shots, rules, keeper, events);
        combat = new CombatSystem(grid, pool, spawn, pickups, arsenal, events, rules);
        status = new StatusSystem(pool, index);
        economy = new EconomySystem(town, rules, keeper);
        clock = timer;
        for (int i = 0; i < 13; i++) elapsed.Add(0);
    }

    public void daylight() {
        input.update(tick);
        keeper.move(grid);
        flow.update();
        light.update();
        events.dispatch();
        tick++;
    }

    public void step() {
        previous = clock();
        input.update(tick);
        record(0);
        spawn.update(tick);
        record(1);
        flow.update();
        record(2);
        ai.update();
        record(3);
        movement.update();
        keeper.move(grid);
        record(4);
        index.rebuild();
        record(5);
        collision.update();
        record(6);
        combat.update(tick, collision.hits);
        record(7);
        status.update(tick);
        record(8);
        pickups.update();
        record(9);
        light.update();
        record(10);
        economy.update(tick);
        record(11);
        events.dispatch();
        record(12);
        tick++;
    }

    private void record(int system) {
        double now = clock();
        elapsed[system] += now - previous;
        previous = now;
    }

    public string checksum() {
        StateHash hash = new StateHash();
        hash.word((uint)(tick));
        hash.word(random.state);
        hash.word((uint)(spawn.cursor));
        hash.coordinate(keeper.x);
        hash.coordinate(keeper.y);
        hash.coordinate(keeper.dx);
        hash.coordinate(keeper.dy);
        hash.coordinate(keeper.aimX);
        hash.coordinate(keeper.aimY);
        hash.coordinate(keeper.speed);
        hash.coordinate(keeper.pull);
        hash.word((uint)(keeper.salvage));
        hash.word((uint)(keeper.dash));
        hash.word((uint)(keeper.recharge));
        hash.word((uint)(keeper.damage));
        hash.word((uint)(keeper.range));
        hash.word((uint)(keeper.haste));
        hash.word((uint)(keeper.orbit));
        hash.word((uint)(keeper.spark));
        hash.word((uint)(keeper.lastShot));
        hash.word((uint)(keeper.xp));
        hash.word((uint)(keeper.level));
        hash.word(keeper.firing ? (uint)(1) : (uint)(0));
        foreach (int offer in keeper.offers) hash.word((uint)(offer));
        for (int i = 0; i < grid.walls.Count; i++) {
            hash.word((uint)((grid.walls[i] ? 1 : 0) + (grid.marsh[i] ? 2 : 0)));
        }
        for (int b = 0; b < grid.beacons.Count; b++) {
            hash.word((uint)(grid.beacons[b]));
            hash.word((uint)(combat.beaconHealth[b]));
        }
        foreach (Structure tower in town.placed) {
            hash.word((uint)(tower.cell));
            hash.word((uint)(tower.kind));
            hash.word((uint)(tower.tier));
            hash.word((uint)(tower.branch));
            hash.word((uint)(tower.hp));
            hash.word((uint)(tower.spent));
            hash.word((uint)(tower.cooldown));
        }
        foreach (Creature enemy in pool.slots) {
            hash.word((uint)(enemy.id));
            hash.word(enemy.active ? (uint)(1) : (uint)(0));
            if (!enemy.active) continue;
            hash.coordinate(enemy.x);
            hash.coordinate(enemy.y);
            hash.coordinate(enemy.vx);
            hash.coordinate(enemy.vy);
            hash.coordinate(enemy.speed);
            hash.word((uint)(enemy.hp));
            hash.word((uint)(enemy.kind));
            hash.word((uint)(enemy.generation));
            hash.word((uint)(enemy.burn));
            hash.word((uint)(enemy.slow));
            hash.word((uint)(enemy.buff));
            hash.word((uint)(enemy.cooldown));
            hash.word((uint)(enemy.warning));
            hash.word((uint)(enemy.phase));
            hash.word((uint)(enemy.flood));
            hash.word(enemy.lit ? (uint)(1) : (uint)(0));
        }
        foreach (Shot shot in shots.shots) {
            if (!shot.active) continue;
            hash.coordinate(shot.x);
            hash.coordinate(shot.y);
            hash.coordinate(shot.dx);
            hash.coordinate(shot.dy);
            hash.coordinate(shot.left);
            hash.word((uint)(shot.damage));
            hash.word((uint)(shot.remaining));
            foreach (int id in shot.hit) hash.word((uint)(id));
        }
        foreach (Shard shard in pickups.shards) {
            if (shard.value == 0) continue;
            hash.coordinate(shard.x);
            hash.coordinate(shard.y);
            hash.word((uint)(shard.value));
        }
        return hash.finish();
    }
}
