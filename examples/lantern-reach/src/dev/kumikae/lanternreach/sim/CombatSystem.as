package dev.kumikae.lanternreach.sim {
    public final class CombatSystem {
        public var beaconHealth:Vector.<int> = new Vector.<int>();
        public var splits:int;
        private var grid:TownGrid;
        private var pool:CreaturePool;
        private var spawn:SpawnSystem;
        private var drops:PickupSystem;
        private var arsenal:Arsenal;
        private var events:EventBus;
        private var rules:Rules;

        public function CombatSystem(map:TownGrid, creatures:CreaturePool, spawner:SpawnSystem,
                pickups:PickupSystem, weapons:Arsenal, bus:EventBus, defs:Rules) {
            grid = map;
            pool = creatures;
            spawn = spawner;
            drops = pickups;
            arsenal = weapons;
            events = bus;
            rules = defs;
            for (var i:int = 0; i < grid.beacons.length; i++) beaconHealth.push(120);
        }

        public function update(tick:int, hits:Vector.<Hit>):void {
            for each (var hit:Hit in hits) {
                hit.target.hurt(hit.damage);
                events.post(WorldEvent.DAMAGE, grid.cell(hit.target.x, hit.target.y), hit.damage);
            }
            arsenal.update(tick);
            var count:int = pool.slots.length;
            for (var i:int = 0; i < count; i++) {
                var actor:Creature = pool.slots[i];
                if (!actor.active) continue;
                var cell:int = grid.cell(actor.x, actor.y);
                if (actor.hp == 0) {
                    death(actor, cell);
                    continue;
                }
                var beacon:int = grid.beacons.indexOf(cell);
                if (beacon < 0 || actor.cooldown > 0) continue;
                var def:EnemyDef = rules.enemies[actor.kind];
                actor.cooldown = def.period;
                beaconHealth[beacon] -= def.damage;
                events.post(WorldEvent.DAMAGE, cell, def.damage);
                if (beaconHealth[beacon] <= 0) {
                    grid.beacons.splice(beacon, 1);
                    beaconHealth.splice(beacon, 1);
                    grid.revision++;
                    events.post(WorldEvent.BEACON_LOST, cell, 0);
                }
            }
        }

        private function death(actor:Creature, cell:int):void {
            drops.drop(actor.x, actor.y, rules.enemies[actor.kind].xp);
            events.post(WorldEvent.DEATH, cell, actor.kind);
            if (actor.kind == Creature.SPLITTER && actor.generation < 3) {
                splits++;
                for (var i:int = 0; i < 2; i++) {
                    var x:Number = Math.max(0, Math.min(grid.width - 0.001, actor.x + (i == 0 ? -0.25 : 0.25)));
                    spawn.place(actor.kind, x, actor.y, actor.generation + 1);
                }
            }
            pool.release(actor);
        }
    }
}
