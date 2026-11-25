package dev.kumikae.lanternreach.sim {
    public final class Tides {
        public var number:int = 1;
        public var phase:String = "day";
        public var ends:int;
        private var game:Simulation;

        public function Tides(sim:Simulation) { game = sim; }

        public function morning():void {
            phase = "day";
            game.light.daylight = 160;
            game.wrack.length = 0;
            for each (var cell:int in game.rules.nights[number - 1].shore) game.wrack.push(cell);
        }

        public function dusk():void {
            var plan:NightPlan = game.rules.nights[number - 1];
            for each (var group:Wave in plan.waves) {
                var wave:Wave = new Wave();
                wave.tick = game.tick + group.tick;
                wave.kind = group.kind;
                wave.count = group.count;
                wave.depth = group.depth;
                wave.name = group.name;
                game.spawn.waves.push(wave);
            }
            ends = game.tick + plan.duration;
            phase = "night";
            game.wrack.length = 0;
        }

        public function daylight():void {
            game.daylight();
            for (var i:int = game.wrack.length - 1; i >= 0; i--) {
                var cell:int = game.wrack[i];
                if (cell < 0) continue;
                var dx:Number = game.keeper.x - cell % game.grid.width - 0.5;
                var dy:Number = game.keeper.y - int(cell / game.grid.width) - 0.5;
                if (dx * dx + dy * dy < 2.25) {
                    game.keeper.salvage += 12;
                    game.wrack[i] = -1;
                }
            }
        }

        public function advance():void {
            var left:int = ends - game.tick;
            var age:int = game.rules.nights[number - 1].duration - left;
            game.light.daylight = Math.max(0, 160 - age);
            game.step();
            if (game.grid.beacons.length == 0) {
                phase = "loss";
                game.events.post(WorldEvent.LOST_WATCH, 0, number);
            } else if (game.tick >= ends) {
                if (number == 5) {
                    for each (var actor:Creature in game.pool.slots) {
                        if (actor.active && actor.kind == Creature.UNDERTOW) return;
                    }
                }
                game.events.post(WorldEvent.WAVE_END, 0, game.stats.waves);
                game.events.post(WorldEvent.DAWN, 0, number);
                game.clearWatch();
                if (number == 5) phase = "win";
                else {
                    number++;
                    morning();
                }
            }
            game.events.dispatch();
        }
    }
}
