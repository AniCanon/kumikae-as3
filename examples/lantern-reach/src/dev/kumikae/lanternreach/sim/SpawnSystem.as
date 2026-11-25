package dev.kumikae.lanternreach.sim {
    public final class SpawnSystem {
        public var waves:Vector.<Wave> = new Vector.<Wave>();
        public var cursor:int;
        private var grid:TownGrid;
        private var pool:CreaturePool;
        private var random:RandomStream;
        private var rules:Rules;
        private var events:EventBus;

        public function SpawnSystem(map:TownGrid, creatures:CreaturePool, stream:RandomStream, defs:Rules, bus:EventBus) {
            grid = map;
            pool = creatures;
            random = stream;
            rules = defs;
            events = bus;
        }

        public function place(kind:int, x:Number, y:Number, generation:int):Creature {
            var enemy:Creature = pool.spawn(x, y);
            var def:EnemyDef = rules.enemies[kind];
            enemy.kind = kind;
            enemy.generation = generation;
            enemy.hp = Math.max(1, int(def.health / (1 + generation)));
            enemy.speed = def.speed * (1 + generation * 0.15);
            return enemy;
        }

        public function update(tick:int):void {
            while (cursor < waves.length && waves[cursor].tick <= tick) {
                var wave:Wave = waves[cursor++];
                if (wave.name > 0) {
                    if (wave.name > 1) events.post(WorldEvent.WAVE_END, 0, wave.name - 1);
                    events.post(WorldEvent.WAVE_START, 0, wave.name);
                }
                for (var i:int = 0; i < wave.count; i++) {
                    var x:Number = random.below(grid.width * 1024) / 1024.0;
                    var y:Number = random.below(wave.depth * 1024) / 1024.0;
                    place(wave.kind, x, y, 0);
                }
            }
        }
    }
}
