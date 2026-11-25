package dev.kumikae.lanternreach {
    import dev.kumikae.lanternreach.sim.*;
    import godot.FileAccess;

    public final class Scenario {
        public var simulation:Simulation;
        public var ticks:int;
        public var menu:Vector.<String> = new Vector.<String>();

        public function Scenario(name:String, timer:Function) {
            var data:Object = JSON.parse(FileAccess.getFileAsString("res://data/" + (name == "station" ? name : "scenarios/" + name) + ".json"));
            var grid:TownGrid = new TownGrid(data.width, data.height);
            for each (var id:int in data.beacons) grid.beacons.push(id);
            for each (var patch:Array in data.marsh) {
                for (var y:int = patch[1]; y < patch[1] + patch[3]; y++) {
                    for (var x:int = patch[0]; x < patch[0] + patch[2]; x++) {
                        grid.marsh[grid.cell(x, y)] = true;
                    }
                }
            }
            simulation = new Simulation(grid, data.seed, Definitions.load(), timer);
            simulation.keeper.salvage = data.salvage;
            for each (var group:Array in data.waves) {
                var wave:Wave = new Wave();
                wave.tick = group[0];
                wave.kind = group[1];
                wave.count = group[2];
                wave.depth = group[3];
                wave.name = group[4];
                simulation.spawn.waves.push(wave);
            }
            for each (var row:Array in data.commands) {
                simulation.commands.enqueue(new TickCommand(row[0], row[1], row[2], row[3]));
            }
            if (data.actors != null) {
                for each (var actor:Array in data.actors) {
                    var resident:Creature = simulation.spawn.place(actor[0], actor[1], actor[2], actor[3]);
                    resident.hp = actor[4];
                    resident.cooldown = actor[5];
                }
            }
            if (data.menu != null) {
                for each (var action:String in data.menu) menu.push(action);
            }
            ticks = data.ticks;
        }
    }
}
