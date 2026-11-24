using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class Buildings {
    public List<Structure> placed = new List<Structure>();
    public List<Structure> at = new List<Structure>();
    private TownGrid grid;
    private Rules rules;
    private Keeper player;
    private BuildRoutes routes;
    private FlowField flow;
    private EventBus events;

    public Buildings(TownGrid map, Rules definitions, Keeper keeper, FlowField paths, EventBus bus) {
        grid = map;
        rules = definitions;
        player = keeper;
        flow = paths;
        events = bus;
        routes = new BuildRoutes(grid);
        for (int i = 0; i < grid.walls.Count; i++) at.Add(null);
    }

    public bool build(int cell, int kind) {
        StructureDef def = rules.structures[kind];
        if (at[cell] != null || grid.beacons.IndexOf(cell) >= 0 || player.salvage < def.cost ||
                (kind == Structure.WALL && !routes.permits(cell))) {
            events.post(WorldEvent.REFUSED, cell, kind);
            return false;
        }
        Structure tower = new Structure();
        tower.kind = kind;
        tower.cell = cell;
        tower.hp = def.health;
        tower.spent = def.cost;
        tower.policy = new Nearest();
        player.salvage -= def.cost;
        at[cell] = tower;
        placed.Add(tower);
        if (kind == Structure.WALL) {
            grid.walls[cell] = true;
            grid.revision++;
        }
        events.post(WorldEvent.BUILT, cell, kind);
        return true;
    }

    public void improve(int cell, int branch) {
        Structure tower = at[cell];
        if (tower == null || tower.tier >= 3) return;
        StructureDef def = rules.structures[tower.kind];
        if (player.salvage < def.cost || (tower.tier == 2 && branch != 1 && branch != 2)) {
            events.post(WorldEvent.REFUSED, cell, tower.kind);
            return;
        }
        player.salvage -= def.cost;
        tower.spent += def.cost;
        tower.tier++;
        tower.hp += def.health * (tower.tier == 3 && branch == 1 ? 2 : 1);
        if (tower.tier == 3) {
            tower.branch = branch;
            if (branch == 1) tower.policy = new Strongest();
            else tower.policy = new FirstAlongPath();
        }
    }

    public void remove(int cell, bool sell) {
        Structure tower = at[cell];
        if (tower == null) return;
        if (sell) player.salvage += (int)(tower.spent / 2);
        if (tower.kind == Structure.WALL) {
            flow.update();
            grid.walls[cell] = false;
            grid.revision++;
            flow.opened(cell);
        }
        at[cell] = null;
        placed.Remove(tower);
        events.post(WorldEvent.DESTROYED, cell, tower.kind);
    }
}
