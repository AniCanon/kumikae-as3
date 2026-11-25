using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class EnemySenses {
    public TownGrid grid;
    public Buildings town;
    public SpatialIndex space;
    public LightField light;
    public SpawnSystem spawn;
    public Rules rules;
    public Keeper player;
    public EventBus events;
    public List<Creature> near = new List<Creature>();

    public EnemySenses(TownGrid map, Buildings buildings, SpatialIndex index, LightField lights,
            SpawnSystem spawner, Rules defs, Keeper keeper, EventBus bus) {
        grid = map;
        town = buildings;
        space = index;
        light = lights;
        spawn = spawner;
        rules = defs;
        player = keeper;
        events = bus;
    }

    public double distance(Creature actor, int cell) {
        double dx = cell % grid.width + 0.5 - actor.x;
        double dy = (int)(cell / grid.width) + 0.5 - actor.y;
        return dx * dx + dy * dy;
    }

    public void seek(Creature actor, int cell) {
        actor.steerX = cell % grid.width + 0.5 - actor.x;
        actor.steerY = (int)(cell / grid.width) + 0.5 - actor.y;
    }

    public int beacon(Creature actor) {
        double best = 1e20;
        int target = -1;
        foreach (int id in grid.beacons) {
            double score = distance(actor, id);
            if (score < best) {
                best = score;
                target = id;
            }
        }
        return target;
    }

    public void strike(Creature actor, Structure tower) {
        if (tower == null || distance(actor, tower.cell) > 2.5 || actor.cooldown > 0) return;
        EnemyDef def = rules.enemies[actor.kind];
        tower.hurt(def.damage);
        if (tower.kind == Structure.WALL && tower.branch == 2) actor.hurt(def.damage);
        actor.cooldown = def.period;
        if (tower.hp == 0) town.remove(tower.cell, false);
    }
}
