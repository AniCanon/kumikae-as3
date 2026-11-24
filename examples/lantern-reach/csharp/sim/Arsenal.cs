using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class Arsenal {
    private TownGrid grid;
    private Buildings buildings;
    private SpatialIndex index;
    private FlowField flow;
    private ShotPool shots;
    private Rules rules;
    private Keeper player;
    private EventBus events;
    private List<Creature> near = new List<Creature>();
    private IStatusEffect burn = new Burn();
    private IStatusEffect slow = new Chill();

    public Arsenal(TownGrid map, Buildings town, SpatialIndex space, FlowField paths,
            ShotPool bolts, Rules defs, Keeper keeper, EventBus bus) {
        grid = map;
        buildings = town;
        index = space;
        flow = paths;
        shots = bolts;
        rules = defs;
        player = keeper;
        events = bus;
    }

    public void update(int tick) {
        weapons(tick);
        foreach (Structure tower in buildings.placed) {
            StructureDef def = rules.structures[tower.kind];
            if (tower.kind < Structure.HARPOON || tower.kind > Structure.BELL) continue;
            if (tower.cooldown > 0) {
                tower.cooldown--;
                continue;
            }
            double x = tower.cell % grid.width + 0.5;
            double y = (int)(tower.cell / grid.width) + 0.5;
            int radius = def.radius + tower.tier - 1 + (tower.branch == 1 ? 2 : 0);
            index.nearby(x, y, radius, near);
            if (near.Count == 0) continue;
            int damage = def.power * tower.tier;
            if (tower.kind == Structure.HARPOON) {
                Creature target = select(x, y, tower.policy);
                if (target == null) continue;
                if (target.kind == Creature.WISP) target.warning = 12;
                shots.fire(x, y, target.x - x, target.y - y, damage, radius, tower.tier + 1);
            } else {
                foreach (Creature enemy in near) {
                    if (tower.kind == Structure.BELL) slow.apply(enemy, 40 * tower.tier);
                    else burn.apply(enemy, damage * 20);
                }
            }
            events.post(WorldEvent.FIRED, tower.cell, tower.kind);
            tower.cooldown = Math.Max(1, def.period - (tower.branch == 2 ? (int)(def.period / 2) : 0));
        }
    }

    private Creature select(double x, double y, ITargeting policy) {
        double best = 1e20;
        Creature found = null;
        foreach (Creature actor in near) {
            if (actor.hp == 0 || (actor.kind == Creature.LURKER && !actor.lit)) continue;
            double dx = actor.x - x;
            double dy = actor.y - y;
            double score = policy.score(actor, dx * dx + dy * dy, flow.cost[grid.cell(actor.x, actor.y)]);
            if (score < best) {
                best = score;
                found = actor;
            }
        }
        return found;
    }

    private void weapons(int tick) {
        WeaponDef primary = rules.weapons[0];
        if (player.lastShot > 0) player.lastShot--;
        if (player.firing && player.lastShot == 0) {
            index.nearby(player.x, player.y, primary.range + player.range, near);
            foreach (Creature enemy in near) {
                double dx = enemy.x - player.x;
                double dy = enemy.y - player.y;
                double front = dx * player.aimX + dy * player.aimY;
                if (front >= Math.Abs(dx * player.aimY - dy * player.aimX)) {
                    enemy.hurt(primary.damage + player.damage);
                    events.post(WorldEvent.DAMAGE, grid.cell(enemy.x, enemy.y), primary.damage + player.damage);
                }
            }
            events.post(WorldEvent.SWING, grid.cell(player.x, player.y), 0);
            player.lastShot = Math.Max(3, primary.period - player.haste);
        }
        for (int i = 1; i < rules.weapons.Count; i++) {
            int strength = i == 1 ? player.orbit : player.spark;
            WeaponDef weapon = rules.weapons[i];
            if (strength == 0 || tick % weapon.period != 0) continue;
            index.nearby(player.x, player.y, weapon.range + player.range, near);
            for (int n = 0; n < near.Count && n < weapon.pierce; n++) {
                near[n].hurt(weapon.damage * strength + player.damage);
            }
        }
    }
}
