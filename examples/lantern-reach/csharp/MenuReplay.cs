using Godot;
using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class MenuReplay {
    public int writes;
    public int reads;
    public int updates;
    private Simulation archive;
    private Register book;
    private string path;
    private List<string> tape;
    private string restored = "";
    private string passage = "";

    public MenuReplay(List<string> actions, Simulation sample, Func<double> timer) {
        tape = actions;
        TownGrid grid = new TownGrid(sample.grid.width, sample.grid.height);
        archive = new Simulation(grid, 1, sample.rules, timer);
        path = "user://lantern-bench-" + OS.GetProcessId() + "-" + Time.GetTicksUsec() + ".json";
        book = new Register(archive, path);
        book.hand = 1;
    }

    public void present(Simulation game, Desk desk) {
        int index = game.tick - 1;
        string action = tape[index % tape.Count];
        int night = 1 + (int)(index / tape.Count) % 5;
        Strings words = desk.words;
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
            for (int j = 0; j < 5; j++)
                desk.option(j, words.get(game.rules.structures[j].key) + "  " + game.rules.structures[j].cost);
            updates += 6;
        } else {
            string subject = action == "ledger" ? "log" : action == "settings" ? "settings" : "levelup";
            passage = words.get("night" + night) + "\n" + words.get("salvage") + "  " + game.keeper.salvage;
            if (action == "ledger") {
                foreach (LogEntry page in book.entries)
                    passage += "\n" + words.get("entry") + " " + page.night + "   " + words.get(game.rules.knowledge[page.lesson].key);
            }
            desk.sheet(words.get(subject), passage, "paper", 0);
            for (int i = 0; i < 5; i++) {
                int id = (index + i) % game.rules.upgrades.Count;
                desk.option(i, words.get(action == "upgrades" ? game.rules.upgrades[id].key : i == 0 ? "back" : "next"));
                if (action == "upgrades") desk.stamp(i, id);
            }
            updates += 7;
        }
    }

    public string checksum(Simulation game) {
        StateHash hash = new StateHash();
        game.hashInto(hash);
        hash.word((uint)(writes));
        hash.word((uint)(reads));
        hash.word((uint)(updates));
        hash.word((uint)(book.entries.Count));
        hash.characters(restored);
        hash.characters(passage);
        return hash.finish();
    }

    public void close() {
        book.close();
        archive.stats.close(archive.events);
        if (FileAccess.FileExists(path)) DirAccess.RemoveAbsolute(path);
    }
}
