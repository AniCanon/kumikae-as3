package dev.kumikae.lanternreach.sim {
    public final class Arsenal {
        private var grid:TownGrid;
        private var buildings:Buildings;
        private var index:SpatialIndex;
        private var flow:FlowField;
        private var shots:ShotPool;
        private var rules:Rules;
        private var player:Keeper;
        private var events:EventBus;
        private var near:Vector.<Creature> = new Vector.<Creature>();
        private var burn:IStatusEffect = new Burn();
        private var slow:IStatusEffect = new Chill();

        public function Arsenal(map:TownGrid, town:Buildings, space:SpatialIndex, paths:FlowField,
                bolts:ShotPool, defs:Rules, keeper:Keeper, bus:EventBus) {
            grid = map;
            buildings = town;
            index = space;
            flow = paths;
            shots = bolts;
            rules = defs;
            player = keeper;
            events = bus;
        }

        public function update(tick:int):void {
            weapons(tick);
            for each (var tower:Structure in buildings.placed) {
                var def:StructureDef = rules.structures[tower.kind];
                if (tower.kind < Structure.HARPOON || tower.kind > Structure.BELL) continue;
                if (tower.cooldown > 0) {
                    tower.cooldown--;
                    continue;
                }
                var x:Number = tower.cell % grid.width + 0.5;
                var y:Number = int(tower.cell / grid.width) + 0.5;
                var radius:int = def.radius + tower.tier - 1 + (tower.branch == 1 ? 2 : 0);
                index.nearby(x, y, radius, near);
                if (near.length == 0) continue;
                var damage:int = def.power * tower.tier;
                if (tower.kind == Structure.HARPOON) {
                    var target:Creature = select(x, y, tower.policy);
                    if (target == null) continue;
                    if (target.kind == Creature.WISP) target.warning = 12;
                    shots.fire(x, y, target.x - x, target.y - y, damage, radius, tower.tier + 1);
                } else {
                    for each (var enemy:Creature in near) {
                        if (tower.kind == Structure.BELL) slow.apply(enemy, 40 * tower.tier);
                        else burn.apply(enemy, damage * 20);
                    }
                }
                events.post(WorldEvent.FIRED, tower.cell, tower.kind);
                tower.cooldown = Math.max(1, def.period - (tower.branch == 2 ? int(def.period / 2) : 0));
            }
        }

        private function select(x:Number, y:Number, policy:ITargeting):Creature {
            var best:Number = 1e20;
            var found:Creature = null;
            for each (var actor:Creature in near) {
                if (actor.hp == 0 || (actor.kind == Creature.LURKER && !actor.lit)) continue;
                var dx:Number = actor.x - x;
                var dy:Number = actor.y - y;
                var score:Number = policy.score(actor, dx * dx + dy * dy, flow.cost[grid.cell(actor.x, actor.y)]);
                if (score < best) {
                    best = score;
                    found = actor;
                }
            }
            return found;
        }

        private function weapons(tick:int):void {
            var primary:WeaponDef = rules.weapons[0];
            if (player.lastShot > 0) player.lastShot--;
            if (player.firing && player.lastShot == 0) {
                index.nearby(player.x, player.y, primary.range + player.range, near);
                for each (var enemy:Creature in near) {
                    var dx:Number = enemy.x - player.x;
                    var dy:Number = enemy.y - player.y;
                    var front:Number = dx * player.aimX + dy * player.aimY;
                    if (front >= Math.abs(dx * player.aimY - dy * player.aimX)) {
                        enemy.hurt(primary.damage + player.damage);
                        events.post(WorldEvent.DAMAGE, grid.cell(enemy.x, enemy.y), primary.damage + player.damage);
                    }
                }
                events.post(WorldEvent.SWING, grid.cell(player.x, player.y), 0);
                player.lastShot = Math.max(3, primary.period - player.haste);
            }
            for (var i:int = 1; i < rules.weapons.length; i++) {
                var strength:int = i == 1 ? player.orbit : player.spark;
                var weapon:WeaponDef = rules.weapons[i];
                if (strength == 0 || tick % weapon.period != 0) continue;
                index.nearby(player.x, player.y, weapon.range + player.range, near);
                for (var n:int = 0; n < near.length && n < weapon.pierce; n++) {
                    near[n].hurt(weapon.damage * strength + player.damage);
                }
            }
        }
    }
}
