package dev.kumikae.lanternreach.sim {
    public final class EnemySenses {
        public var grid:TownGrid;
        public var town:Buildings;
        public var space:SpatialIndex;
        public var light:LightField;
        public var spawn:SpawnSystem;
        public var rules:Rules;
        public var player:Keeper;
        public var near:Vector.<Creature> = new Vector.<Creature>();

        public function EnemySenses(map:TownGrid, buildings:Buildings, index:SpatialIndex, lights:LightField,
                spawner:SpawnSystem, defs:Rules, keeper:Keeper) {
            grid = map;
            town = buildings;
            space = index;
            light = lights;
            spawn = spawner;
            rules = defs;
            player = keeper;
        }

        public function distance(actor:Creature, cell:int):Number {
            var dx:Number = cell % grid.width + 0.5 - actor.x;
            var dy:Number = int(cell / grid.width) + 0.5 - actor.y;
            return dx * dx + dy * dy;
        }

        public function seek(actor:Creature, cell:int):void {
            actor.steerX = cell % grid.width + 0.5 - actor.x;
            actor.steerY = int(cell / grid.width) + 0.5 - actor.y;
        }

        public function beacon(actor:Creature):int {
            var best:Number = 1e20;
            var target:int = -1;
            for each (var id:int in grid.beacons) {
                var score:Number = distance(actor, id);
                if (score < best) {
                    best = score;
                    target = id;
                }
            }
            return target;
        }

        public function strike(actor:Creature, tower:Structure):void {
            if (tower == null || distance(actor, tower.cell) > 2.5 || actor.cooldown > 0) return;
            var def:EnemyDef = rules.enemies[actor.kind];
            tower.hurt(def.damage);
            if (tower.kind == Structure.WALL && tower.branch == 2) actor.hurt(def.damage);
            actor.cooldown = def.period;
            if (tower.hp == 0) town.remove(tower.cell, false);
        }
    }
}
