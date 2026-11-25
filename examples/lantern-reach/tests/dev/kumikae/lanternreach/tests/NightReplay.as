package dev.kumikae.lanternreach.tests {
    import dev.kumikae.lanternreach.*;
    import godot.FileAccess;
    import dev.kumikae.lanternreach.sim.*;
    import godot.Node;
    import godot.DirAccess;
    import godot.OS;
    public class NightReplay extends Node {
        private function time():Number { return 0; }
        override public function _ready():void {
            var game:Simulation = new Scenario("station", time).simulation;
            var tides:Tides = new Tides(game);
            tides.morning();
            var path:String = "user://probe-" + OS.getProcessID() + ".json";
            var book:Register = new Register(game, path);
            var rows:Array = JSON.parse(FileAccess.getFileAsString("res://tests/first-watch.json")) as Array;
            for each (var command:Array in rows)
                game.commands.enqueue(new TickCommand(command[0], command[1], command[2], command[3]));
            tides.daylight();
            tides.dusk();
            var checks:Array = [];
            while (tides.phase == "night") {
                tides.advance();
                if (game.tick % 60 == 0) checks.push(game.checksum());
            }
            checks.push(game.checksum());
            book.saved = Dawn.write(game, tides.number);
            book.save();
            book.load();
            var other:Simulation = new Scenario("station", time).simulation;
            var restored:int = Dawn.read(other, book.saved);
            trace(JSON.stringify({checksums:checks, night:restored, lit:game.grid.beacons.length,
                tick:game.tick, entries:book.entries.length, unlocked:book.knows("structure", 3), restored:other.checksum()}));
            book.close();
            game.stats.close(game.events);
            other.stats.close(other.events);
            DirAccess.removeAbsolute(path);
            getTree().quit(restored == 2 && game.checksum() == other.checksum() ? 0 : 1);
        }
    }
}
