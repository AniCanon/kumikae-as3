using Godot;
using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public partial class Watch : Node {
    private Simulation game;
    private Desk desk;
    private Orders orders;
    private MarshView marsh;
    private StationAudio station;
    public string screen = "title";
    private string field = "day";
    private string back = "title";
    private Tides tides;
    private Register book;
    private int pageIndex;
    private int sunrise;
    private bool shake = true;

    public void setup(Simulation sim, Desk hud, Orders input, MarshView view, StationAudio music) {
        game = sim;
        desk = hud;
        orders = input;
        marsh = view;
        station = music;
        tides = new Tides(game);
        book = new Register(game, ProjectSettings.GetSetting("lantern/register", "user://lantern-register.json").AsString());
        station.volume = book.volume;
        shake = book.shake;
        marsh.shake = shake;
        marsh.accessible = book.accessible;
        show("title");
    }

    private string word(string key) { return desk.words.get(key); }
    private void option(int slot, string key) { desk.option(slot, word(key)); }
    private void page(string title, string body, string imageKey, int index) {
        desk.sheet(word(title), body, imageKey, index);
    }

    private void show(string key) {
        int night = tides.number;
        screen = key;
        orders.active = key == "day" || key == "night";
        orders.Visible = orders.active;
        orders.day = key == "day";
        if (orders.active) {
            field = key;
            desk.field();
            option(0, key == "day" ? "tools" : "dash");
            option(1, key == "day" ? "watch" : "aim");
            option(2, "pause");
            if (key == "day") desk.option(3, word(orders.tool));
        } else if (key == "title") {
            page("title", word("subtitle"), "title", 0);
            option(0, "start"); option(1, "log"); option(2, "settings"); option(3, "controls"); option(4, "credits");
            if (book.saved != "") option(5, "continueRun");
        } else if (key == "credits") {
            page(key, word("creditsText"), "title", 0);
            option(0, "back");
        } else if (key == "dawn") {
            page(key, word("saved"), "paper", 0);
            option(0, "next"); option(1, "quit");
        } else if (key == "open" || key == "card") {
            desk.sheet(key == "open" ? word("open") : word("card") + " " + night,
                key == "open" ? word("arrival") + "\n\n" + word("opening") : word("night" + night), "cards", key == "open" ? 7 : night - 1);
            option(0, key == "open" ? "next" : "keep");
        } else if (key == "paused") {
            page("paused", word("watchBrief"), "paper", 0);
            option(0, "resume"); option(1, "settings"); option(2, "log"); option(3, "controls"); option(4, "quit");
        } else if (key == "settings") {
            page("settings", "", "paper", 0);
            desk.option(0, word("volume") + " " + (int)(station.volume * 100) + "%");
            desk.option(1, word("shake") + " " + word(shake ? "on" : "off"));
            desk.option(2, word("palette") + " " + word(marsh.accessible ? "on" : "off"));
            option(3, "back");
        } else if (key == "log") {
            if (book.entries.Count == 0) page(key, word("logEmpty"), "paper", 0);
            else {
                LogEntry log = book.entries[pageIndex];
                string line = word(log.lost ? "lost" : log.night == 5 ? "won" : "night" + log.night);
                desk.sheet(word("keeper") + " " + log.hand + "   ·   " + word("entry") + " " + (pageIndex + 1),
                    line + "\n\n" + word(game.rules.knowledge[log.lesson].key), "paper", 0);
                desk.handwriting(log.hand);
            }
            option(0, "back");
            if (pageIndex > 0) option(1, "prev");
            if (pageIndex + 1 < book.entries.Count) option(2, "next");
        } else if (key == "controls") {
            page(key, word("help"), "paper", 0);
            option(0, "back");
        } else if (key == "tools" || key == "more") {
            string forecast = word("chooseTile");
            foreach (Advice note in game.rules.knowledge) {
                if (note.subject == "waves" && note.value == night && book.knows("waves", night)) forecast += "\n" + word(note.key);
            }
            page("tool", forecast, "paper", 0);
            if (key == "tools") {
                for (int i = 0; i < 5; i++) {
                    if (i < 3 || book.knows("structure", i))
                        desk.option(i, word(game.rules.structures[i].key) + "  " + game.rules.structures[i].cost);
                }
                option(5, "more");
            } else {
                if (book.knows("structure", 5)) desk.option(0, word("rig") + "  " + game.rules.structures[5].cost);
                option(1, "upgrade"); option(2, "branchA"); option(3, "branchB"); option(4, "sell"); option(5, "back");
            }
        } else if (key == "levelup") {
            page(key, "", "paper", 0);
            for (int n = 0; n < game.keeper.offers.Count; n++) {
                option(n, game.rules.upgrades[game.keeper.offers[n]].key);
                desk.stamp(n, game.keeper.offers[n]);
            }
        } else if (key == "loss" || key == "win") {
            page(key, word(key == "loss" ? "lost" : "won"), "cards", key == "loss" ? 6 : 5);
            option(0, "end");
        }
    }

    public void close() { book.close(); }

    private void begin(bool resuming) {
        if (resuming) tides.number = Dawn.read(game, book.saved);
        else {
            book.hand++;
            book.best = Math.Max(1, book.best);
            book.saved = "";
            book.save();
        }
        tides.morning();
        for (int i = 0; i < 3; i++) marsh.marked[i] = book.knows("shore", i);
        show(resuming ? "card" : "open");
    }

    private void select(int index) {
        if (screen == "title") {
            back = "title";
            if (index == 0 || index == 5) begin(index == 5);
            else show(index == 1 ? "log" : index == 2 ? "settings" : index == 3 ? "controls" : "credits");
        } else if (screen == "log" && index > 0) {
            pageIndex += index == 1 ? -1 : 1;
            show("log");
        } else if (screen == "dawn") {
            if (index == 0) show("card");
            else GetTree().ReloadCurrentScene();
        } else if (screen == "open") show("card");
        else if (screen == "card") show("day");
        else if (screen == "day") {
            if (index == 1) { tides.dusk(); show("night"); }
            else show(index == 2 ? "paused" : "tools");
        } else if (screen == "night") {
            if (index == 0) orders.send("dash", 0, 0);
            if (index == 1) orders.send("fire", game.keeper.firing ? 0 : 1, 0);
            if (index == 2) show("paused");
        } else if (screen == "tools" || screen == "more") {
            if (index == 5) show(screen == "tools" ? "more" : "day");
            else {
                orders.tool = screen == "tools" ? game.rules.structures[index].key :
                    index == 0 ? "rig" : index == 1 ? "upgrade" : index == 2 ? "branchA" : index == 3 ? "branchB" : "sell";
                show("day");
            }
        } else if (screen == "paused") {
            back = "paused";
            if (index == 4) GetTree().ReloadCurrentScene();
            else show(index == 0 ? field : index == 1 ? "settings" : index == 2 ? "log" : "controls");
        } else if (screen == "settings") {
            if (index == 0) station.volume = ((int)(station.volume * 4 + 1) % 5) / 4.0;
            if (index == 1) {
                shake = !shake;
                marsh.shake = shake;
            }
            if (index == 2) marsh.accessible = !marsh.accessible;
            book.volume = station.volume;
            book.shake = shake;
            book.accessible = marsh.accessible;
            book.save();
            show(index == 3 ? back : "settings");
        } else if (screen == "levelup") {
            orders.send("choose", index, 0);
            show("night");
        } else if (screen == "loss" || screen == "win") GetTree().ReloadCurrentScene();
        else show(back);
    }

    public void poll() {
        if (Input.IsActionJustPressed("pause")) {
            if (screen == "night" || screen == "day") show("paused");
            else if (screen == "paused") show(field);
        }
        if (desk.selected >= 0) {
            int index = desk.selected;
            desk.selected = -1;
            select(index);
        }
        if (orders.active) orders.poll();
    }

    public void advance() {
        if (screen == "day") tides.daylight();
        if (screen == "sunrise") {
            sunrise++;
            marsh.pullBack(sunrise);
            if (sunrise >= 150) show("win");
        }
        if (screen != "night") return;
        tides.advance();
        if (tides.phase != "night") {
            book.saved = tides.phase == "day" ? Dawn.write(game, tides.number) : "";
            book.best = Math.Max(book.best, tides.number);
            book.save();
            if (tides.phase == "win") {
                screen = "sunrise";
                orders.active = false;
            } else show(tides.phase == "day" ? "dawn" : "loss");
        } else if (game.keeper.offers.Count > 0) show("levelup");
    }

    public void hud() { desk.hud(game, tides.number, field == "day", tides.ends - game.tick); }
}
