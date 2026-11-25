package dev.kumikae.lanternreach {
    import dev.kumikae.lanternreach.sim.*;
    import godot.Node;
    import godot.Input;

    public class Watch extends Node {
        private var game:Simulation;
        private var desk:Desk;
        private var orders:Orders;
        private var marsh:MarshView;
        private var station:StationAudio;
        public var screen:String = "title";
        private var field:String = "day";
        private var back:String = "title";
        private var tides:Tides;
        private var book:Register;
        private var pageIndex:int;
        private var sunrise:int;
        private var shake:Boolean = true;

        public function setup(sim:Simulation, hud:Desk, input:Orders, view:MarshView, music:StationAudio):void {
            game = sim;
            desk = hud;
            orders = input;
            marsh = view;
            station = music;
            tides = new Tides(game);
            book = new Register(game, "user://lantern-register.json");
            station.volume = book.volume;
            shake = book.shake;
            marsh.shake = shake;
            marsh.accessible = book.accessible;
            show("title");
        }

        private function word(key:String):String { return desk.words.get(key); }
        private function option(slot:int, key:String):void { desk.option(slot, word(key)); }
        private function page(title:String, body:String, imageKey:String, index:int):void {
            desk.sheet(word(title), body, imageKey, index);
        }

        private function show(key:String):void {
            var night:int = tides.number;
            screen = key;
            orders.active = key == "day" || key == "night";
            orders.visible = orders.active;
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
                desk.option(0, word("volume") + " " + int(station.volume * 100) + "%");
                desk.option(1, word("shake") + " " + word(shake ? "on" : "off"));
                desk.option(2, word("palette") + " " + word(marsh.accessible ? "on" : "off"));
                option(3, "back");
            } else if (key == "log") {
                if (book.entries.length == 0) page(key, word("logEmpty"), "paper", 0);
                else {
                    var log:LogEntry = book.entries[pageIndex];
                    var line:String = word(log.lost ? "lost" : log.night == 5 ? "won" : "night" + log.night);
                    desk.sheet(word("keeper") + " " + log.hand + "   ·   " + word("entry") + " " + (pageIndex + 1),
                        line + "\n\n" + word(game.rules.knowledge[log.lesson].key), "paper", 0);
                    desk.handwriting(log.hand);
                }
                option(0, "back");
                if (pageIndex > 0) option(1, "prev");
                if (pageIndex + 1 < book.entries.length) option(2, "next");
            } else if (key == "controls") {
                page(key, word("help"), "paper", 0);
                option(0, "back");
            } else if (key == "tools" || key == "more") {
                var forecast:String = word("chooseTile");
                for each (var note:Advice in game.rules.knowledge) {
                    if (note.subject == "waves" && note.value == night && book.knows("waves", night)) forecast += "\n" + word(note.key);
                }
                page("tool", forecast, "paper", 0);
                if (key == "tools") {
                    for (var i:int = 0; i < 5; i++) {
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
                for (var n:int = 0; n < game.keeper.offers.length; n++) {
                    option(n, game.rules.upgrades[game.keeper.offers[n]].key);
                    desk.stamp(n, game.keeper.offers[n]);
                }
            } else if (key == "loss" || key == "win") {
                page(key, word(key == "loss" ? "lost" : "won"), "cards", key == "loss" ? 6 : 5);
                option(0, "end");
            }
        }

        public function close():void { book.close(); }

        private function begin(resuming:Boolean):void {
            if (resuming) tides.number = Dawn.read(game, book.saved);
            else {
                book.hand++;
                book.best = Math.max(1, book.best);
                book.saved = "";
                book.save();
            }
            tides.morning();
            for (var i:int = 0; i < 3; i++) marsh.marked[i] = book.knows("shore", i);
            show(resuming ? "card" : "open");
        }

        private function select(index:int):void {
            if (screen == "title") {
                back = "title";
                if (index == 0 || index == 5) begin(index == 5);
                else show(index == 1 ? "log" : index == 2 ? "settings" : index == 3 ? "controls" : "credits");
            } else if (screen == "log" && index > 0) {
                pageIndex += index == 1 ? -1 : 1;
                show("log");
            } else if (screen == "dawn") {
                if (index == 0) show("card");
                else getTree().reloadCurrentScene();
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
                if (index == 4) getTree().reloadCurrentScene();
                else show(index == 0 ? field : index == 1 ? "settings" : index == 2 ? "log" : "controls");
            } else if (screen == "settings") {
                if (index == 0) station.volume = (int(station.volume * 4 + 1) % 5) / 4.0;
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
            } else if (screen == "loss" || screen == "win") getTree().reloadCurrentScene();
            else show(back);
        }

        public function poll():void {
            if (Input.isActionJustPressed("pause")) {
                if (screen == "night" || screen == "day") show("paused");
                else if (screen == "paused") show(field);
            }
            if (desk.selected >= 0) {
                var index:int = desk.selected;
                desk.selected = -1;
                select(index);
            }
            if (orders.active) orders.poll();
        }

        public function advance():void {
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
                book.best = Math.max(book.best, tides.number);
                book.save();
                if (tides.phase == "win") {
                    screen = "sunrise";
                    orders.active = false;
                } else show(tides.phase == "day" ? "dawn" : "loss");
            } else if (game.keeper.offers.length > 0) show("levelup");
        }

        public function hud():void { desk.hud(game, tides.number, field == "day"); }
    }
}
