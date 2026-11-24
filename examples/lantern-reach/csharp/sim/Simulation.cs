using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class Simulation {
    public TownGrid grid;
    public FlowField flow;
    public RandomStream random;
    public Keeper keeper = new Keeper();
    public CreaturePool pool = new CreaturePool();
    public CommandQueue commands = new CommandQueue();
    public int tick;
    public List<double> elapsed = new List<double>();
    private InputSystem input;
    private SpawnSystem spawn;
    private SpatialIndex index;
    private SwarmSteering ai;
    private Movement movement;
    private Func<double> clock;
    private double previous;

    public Simulation(TownGrid map, uint seed, int population, Func<double> timer) {
        grid = map;
        random = new RandomStream(seed);
        flow = new FlowField(grid);
        index = new SpatialIndex(grid, pool);
        ai = new SwarmSteering(grid, pool, index, flow);
        movement = new Movement(grid, pool);
        spawn = new SpawnSystem(grid, pool, random, population);
        input = new InputSystem(grid, keeper, commands, flow);
        clock = timer;
        for (int i = 0; i < 6; i++) elapsed.Add(0);
    }

    public void step() {
        previous = clock();
        input.update(tick);
        record(0);
        spawn.update();
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
        hash.coordinate(keeper.x);
        hash.coordinate(keeper.y);
        hash.coordinate(keeper.dx);
        hash.coordinate(keeper.dy);
        hash.word((uint)(keeper.salvage));
        for (int i = 0; i < grid.walls.Count; i++) {
            hash.word((uint)((grid.walls[i] ? 1 : 0) + (grid.marsh[i] ? 2 : 0)));
        }
        for (int b = 0; b < grid.beacons.Count; b++) hash.word((uint)(grid.beacons[b]));
        for (int j = 0; j < pool.slots.Count; j++) {
            Creature enemy = pool.slots[j];
            hash.word((uint)(enemy.id));
            hash.word(enemy.active ? 1u : 0u);
            if (!enemy.active) continue;
            hash.coordinate(enemy.x);
            hash.coordinate(enemy.y);
            hash.coordinate(enemy.vx);
            hash.coordinate(enemy.vy);
            hash.word((uint)(enemy.hp));
        }
        return hash.finish();
    }
}
