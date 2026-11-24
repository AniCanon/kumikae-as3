package dev.kumikae.lanternreach.sim {
    public final class Simulation {
        public var grid:TownGrid;
        public var flow:FlowField;
        public var random:RandomStream;
        public var keeper:Keeper = new Keeper();
        public var pool:CreaturePool = new CreaturePool();
        public var commands:CommandQueue = new CommandQueue();
        public var tick:int;
        public var elapsed:Vector.<Number> = new Vector.<Number>();
        private var input:InputSystem;
        private var spawn:SpawnSystem;
        private var index:SpatialIndex;
        private var ai:SwarmSteering;
        private var movement:Movement;
        private var clock:Function;
        private var previous:Number;

        public function Simulation(map:TownGrid, seed:uint, population:int, timer:Function) {
            grid = map;
            random = new RandomStream(seed);
            flow = new FlowField(grid);
            index = new SpatialIndex(grid, pool);
            ai = new SwarmSteering(grid, pool, index, flow);
            movement = new Movement(grid, pool);
            spawn = new SpawnSystem(grid, pool, random, population);
            input = new InputSystem(grid, keeper, commands, flow);
            clock = timer;
            for (var i:int = 0; i < 6; i++) elapsed.push(0);
        }

        public function step():void {
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

        private function record(system:int):void {
            var now:Number = clock();
            elapsed[system] += now - previous;
            previous = now;
        }

        public function checksum():String {
            var hash:StateHash = new StateHash();
            hash.word(uint(tick));
            hash.word(random.state);
            hash.coordinate(keeper.x);
            hash.coordinate(keeper.y);
            hash.coordinate(keeper.dx);
            hash.coordinate(keeper.dy);
            hash.word(uint(keeper.salvage));
            for (var i:int = 0; i < grid.walls.length; i++) {
                hash.word(uint((grid.walls[i] ? 1 : 0) + (grid.marsh[i] ? 2 : 0)));
            }
            for (var b:int = 0; b < grid.beacons.length; b++) hash.word(uint(grid.beacons[b]));
            for (var j:int = 0; j < pool.slots.length; j++) {
                var enemy:Creature = pool.slots[j];
                hash.word(uint(enemy.id));
                hash.word(enemy.active ? 1 : 0);
                if (!enemy.active) continue;
                hash.coordinate(enemy.x);
                hash.coordinate(enemy.y);
                hash.coordinate(enemy.vx);
                hash.coordinate(enemy.vy);
                hash.word(uint(enemy.hp));
            }
            return hash.finish();
        }
    }
}
