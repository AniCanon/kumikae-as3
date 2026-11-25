package dev.kumikae.lanternreach {
    import dev.kumikae.lanternreach.sim.*;
    import godot.DirAccess;
    import godot.OS;
    import godot.FileAccess;
    import godot.Time;

    public final class MenuReplay {
        public var writes:int;
        public var reads:int;
        public var updates:int;
        private var archive:Simulation;
        private var book:Register;
        private var path:String;
        private var tape:Vector.<String>;
        private var restored:String = "";
        private var passage:String = "";

        public function MenuReplay(actions:Vector.<String>, sample:Simulation, timer:Function) {
            tape = actions;
            var grid:TownGrid = new TownGrid(sample.grid.width, sample.grid.height);
            archive = new Simulation(grid, 1, sample.rules, timer);
            path = "user://lantern-bench-" + OS.getProcessID() + "-" + Time.getTicksUsec() + ".json";
            book = new Register(archive, path);
            book.hand = 1;
        }

        public function present(game:Simulation, desk:Desk):void {
            var index:int = game.tick - 1;
            var action:String = tape[index % tape.length];
            var night:int = 1 + int(index / tape.length) % 5;
            var words:Strings = desk.words;
            if (action == "save") {
                archive.events.post(WorldEvent.DAWN, 0, night);
                archive.events.dispatch();
                book.saved = Dawn.write(game, night);
                book.save();
                writes++;
            } else if (action == "load") {
                book.load();
                Dawn.read(archive, book.saved);
                restored = archive.checksum();
                reads++;
            } else if (action == "hud") {
                desk.field();
                desk.hud(game, night, true);
                for (var j:int = 0; j < 5; j++)
                    desk.option(j, words.get(game.rules.structures[j].key) + "  " + game.rules.structures[j].cost);
                updates += 6;
            } else {
                var subject:String = action == "ledger" ? "log" : action == "settings" ? "settings" : "levelup";
                passage = words.get("night" + night) + "\n" + words.get("salvage") + "  " + game.keeper.salvage;
                if (action == "ledger") {
                    for each (var page:LogEntry in book.entries)
                        passage += "\n" + words.get("entry") + " " + page.night + "   " + words.get(game.rules.knowledge[page.lesson].key);
                }
                desk.sheet(words.get(subject), passage, "paper", 0);
                for (var i:int = 0; i < 5; i++) {
                    var id:int = (index + i) % game.rules.upgrades.length;
                    desk.option(i, words.get(action == "upgrades" ? game.rules.upgrades[id].key : i == 0 ? "back" : "next"));
                    if (action == "upgrades") desk.stamp(i, id);
                }
                updates += 7;
            }
        }

        public function checksum(game:Simulation):String {
            var hash:StateHash = new StateHash();
            game.hashInto(hash);
            hash.word(uint(writes));
            hash.word(uint(reads));
            hash.word(uint(updates));
            hash.word(uint(book.entries.length));
            hash.characters(restored);
            hash.characters(passage);
            return hash.finish();
        }

        public function close():void {
            book.close();
            archive.stats.close(archive.events);
            if (FileAccess.fileExists(path)) DirAccess.removeAbsolute(path);
        }
    }
}
