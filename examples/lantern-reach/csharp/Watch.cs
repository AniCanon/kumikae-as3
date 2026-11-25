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
    private int night = 1;
    private int ends;
    private bool shake = true;

    public void setup(Simulation sim, Desk hud, Orders input, MarshView view, StationAudio music) {
        game = sim;
        desk = hud;
        orders = input;
        marsh = view;
        station = music;
        show("title");
    }

    private string word(string key) { return desk.words.get(key); }
    private void option(int slot, string key) { desk.option(slot, word(key)); }
    private void page(string title, string body, string imageKey, int index) {
        desk.sheet(word(title), body, imageKey, index);
    }

    private void show(string key) {
        screen = key;
        orders.active = key == "day" || key == "night";
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
            option(0, "start"); option(1, "log"); option(2, "settings"); option(3, "controls");
        } else if (key == "open" || key == "card") {
            desk.sheet(key == "open" ? word("open") : word("card") + " " + night,
                word(key == "open" ? "opening" : "night" + night), "cards", key == "open" ? 7 : night - 1);
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
        } else if (key == "controls" || key == "log") {
            page(key, word(key == "log" ? "logEmpty" : "help"), "paper", 0);
            option(0, "back");
        } else if (key == "tools" || key == "more") {
            page("tool", word("chooseTile"), "paper", 0);
            if (key == "tools") {
                for (int i = 0; i < 5; i++) desk.option(i, word(game.rules.structures[i].key) + "  " + game.rules.structures[i].cost);
                option(5, "more");
            } else {
                desk.option(0, word("rig") + "  " + game.rules.structures[5].cost);
                option(1, "upgrade"); option(2, "branchA"); option(3, "branchB"); option(4, "sell"); option(5, "back");
            }
        } else if (key == "levelup") {
            page(key, "", "paper", 0);
            for (int n = 0; n < game.keeper.offers.Count; n++) option(n, game.rules.upgrades[game.keeper.offers[n]].key);
        } else if (key == "loss" || key == "win") {
            page(key, word(key == "loss" ? "lost" : "won"), "cards", key == "loss" ? 6 : 5);
            option(0, "end");
        }
    }

    private void dusk() {
        Wave wave = new Wave();
        wave.kind = 0;
        wave.count = 24;
        wave.depth = 3;
        wave.name = 1;
        wave.tick = game.tick;
        game.spawn.waves.Add(wave);
        ends = game.tick + 3600;
        show("night");
    }

    private void select(int index) {
        if (screen == "title") {
            back = "title";
            show(index == 0 ? "open" : index == 1 ? "log" : index == 2 ? "settings" : "controls");
        } else if (screen == "open") show("card");
        else if (screen == "card") show("day");
        else if (screen == "day") {
            if (index == 1) dusk();
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
        if (screen == "day") game.daylight();
        if (screen != "night") return;
        game.step();
        if (game.grid.beacons.Count == 0) show("loss");
        else if (game.tick >= ends) {
            night++;
            show(night == 6 ? "win" : "card");
        } else if (game.keeper.offers.Count > 0) show("levelup");
    }

    public void hud() { desk.hud(game, night, field == "day"); }
}
