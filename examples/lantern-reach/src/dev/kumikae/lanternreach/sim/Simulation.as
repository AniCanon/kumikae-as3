package dev.kumikae.lanternreach.sim {
    public final class Simulation {
        public var grid:TownGrid;
        public var rules:Rules;
        public var flow:FlowField;
        public var random:RandomStream;
        public var keeper:Keeper = new Keeper();
        public var pool:CreaturePool = new CreaturePool();
        public var commands:CommandQueue = new CommandQueue();
        public var events:EventBus = new EventBus();
        public var stats:RunStats;
        public var shots:ShotPool = new ShotPool();
        public var town:Buildings;
        public var spawn:SpawnSystem;
        public var light:LightField;
        public var pickups:PickupSystem;
        public var combat:CombatSystem;
        public var tick:int;
        public var elapsed:Vector.<Number> = new Vector.<Number>();
        private var input:InputSystem;
        private var index:SpatialIndex;
        private var ai:SwarmSteering;
        private var movement:Movement;
        private var collision:CollisionSystem;
        private var status:StatusSystem;
        private var economy:EconomySystem;
        private var clock:Function;
        private var previous:Number;

        public function Simulation(map:TownGrid, seed:uint, defs:Rules, timer:Function) {
            grid = map;
            rules = defs;
            stats = new RunStats(events);
            random = new RandomStream(seed);
            flow = new FlowField(grid);
            index = new SpatialIndex(grid, pool);
            town = new Buildings(grid, rules, keeper, flow, events);
            light = new LightField(grid, town, pool, rules);
            spawn = new SpawnSystem(grid, pool, random, rules, events);
            var senses:EnemySenses = new EnemySenses(grid, town, index, light, spawn, rules, keeper);
            ai = new SwarmSteering(grid, pool, index, flow, senses);
            movement = new Movement(grid, pool);
            var levels:LevelUps = new LevelUps(keeper, rules, random, events);
            input = new InputSystem(grid, keeper, commands, town, levels, rules);
            pickups = new PickupSystem(grid, keeper, levels);
            collision = new CollisionSystem(index, shots);
            var arsenal:Arsenal = new Arsenal(grid, town, index, flow, shots, rules, keeper, events);
            combat = new CombatSystem(grid, pool, spawn, pickups, arsenal, events, rules);
            status = new StatusSystem(pool, index);
            economy = new EconomySystem(town, rules, keeper);
            clock = timer;
            for (var i:int = 0; i < 13; i++) elapsed.push(0);
        }

        public function daylight():void {
            input.update(tick);
            keeper.move(grid);
            flow.update();
            light.update();
            events.dispatch();
            tick++;
        }

        public function step():void {
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

        private function record(system:int):void {
            var now:Number = clock();
            elapsed[system] += now - previous;
            previous = now;
        }

        public function checksum():String {
            var hash:StateHash = new StateHash();
            hash.word(uint(tick));
            hash.word(random.state);
            hash.word(uint(spawn.cursor));
            hash.coordinate(keeper.x);
            hash.coordinate(keeper.y);
            hash.coordinate(keeper.dx);
            hash.coordinate(keeper.dy);
            hash.coordinate(keeper.aimX);
            hash.coordinate(keeper.aimY);
            hash.coordinate(keeper.speed);
            hash.coordinate(keeper.pull);
            hash.word(uint(keeper.salvage));
            hash.word(uint(keeper.dash));
            hash.word(uint(keeper.recharge));
            hash.word(uint(keeper.damage));
            hash.word(uint(keeper.range));
            hash.word(uint(keeper.haste));
            hash.word(uint(keeper.orbit));
            hash.word(uint(keeper.spark));
            hash.word(uint(keeper.lastShot));
            hash.word(uint(keeper.xp));
            hash.word(uint(keeper.level));
            hash.word(keeper.firing ? uint(1) : uint(0));
            for each (var offer:int in keeper.offers) hash.word(uint(offer));
            for (var i:int = 0; i < grid.walls.length; i++) {
                hash.word(uint((grid.walls[i] ? 1 : 0) + (grid.marsh[i] ? 2 : 0)));
            }
            for (var b:int = 0; b < grid.beacons.length; b++) {
                hash.word(uint(grid.beacons[b]));
                hash.word(uint(combat.beaconHealth[b]));
            }
            for each (var tower:Structure in town.placed) {
                hash.word(uint(tower.cell));
                hash.word(uint(tower.kind));
                hash.word(uint(tower.tier));
                hash.word(uint(tower.branch));
                hash.word(uint(tower.hp));
                hash.word(uint(tower.spent));
                hash.word(uint(tower.cooldown));
            }
            for each (var enemy:Creature in pool.slots) {
                hash.word(uint(enemy.id));
                hash.word(enemy.active ? uint(1) : uint(0));
                if (!enemy.active) continue;
                hash.coordinate(enemy.x);
                hash.coordinate(enemy.y);
                hash.coordinate(enemy.vx);
                hash.coordinate(enemy.vy);
                hash.coordinate(enemy.speed);
                hash.word(uint(enemy.hp));
                hash.word(uint(enemy.kind));
                hash.word(uint(enemy.generation));
                hash.word(uint(enemy.burn));
                hash.word(uint(enemy.slow));
                hash.word(uint(enemy.buff));
                hash.word(uint(enemy.cooldown));
                hash.word(uint(enemy.warning));
                hash.word(uint(enemy.phase));
                hash.word(uint(enemy.flood));
                hash.word(enemy.lit ? uint(1) : uint(0));
            }
            for each (var shot:Shot in shots.shots) {
                if (!shot.active) continue;
                hash.coordinate(shot.x);
                hash.coordinate(shot.y);
                hash.coordinate(shot.dx);
                hash.coordinate(shot.dy);
                hash.coordinate(shot.left);
                hash.word(uint(shot.damage));
                hash.word(uint(shot.remaining));
                for each (var id:int in shot.hit) hash.word(uint(id));
            }
            for each (var shard:Shard in pickups.shards) {
                if (shard.value == 0) continue;
                hash.coordinate(shard.x);
                hash.coordinate(shard.y);
                hash.word(uint(shard.value));
            }
            return hash.finish();
        }
        public var wrack:Vector.<int> = new Vector.<int>();

        public function clearWatch():void {
            spawn.waves.length = 0;
            spawn.cursor = 0;
            pool.clear();
            shots.clear();
            pickups.clear();
            index.rebuild();
            keeper.dx = 0;
            keeper.dy = 0;
            keeper.dash = 0;
            keeper.recharge = 0;
            keeper.lastShot = 0;
            keeper.firing = false;
            light.daylight = 160;
            grid.revision++;
            flow.update();
            light.update();
        }

    }
}
