package dev.kumikae.lanternreach {
    import dev.kumikae.lanternreach.sim.*;
    import godot.Node2D;
    import godot.Time;

    public class Reach extends Node2D {
        private var game:Simulation;
        private var stepper:FixedStep = new FixedStep();
        private var marsh:MarshView;
        private var station:StationAudio;
        private var effects:Impressions;
        private var desk:Desk;
        private var orders:Orders;
        private var watch:Watch;
        private var limit:int;
        private var checks:Array = [];
        private var total:Number = 0;
        private var presentation:Number = 0;

        private function milliseconds():Number {
            return Number(Time.getTicksUsec()) / 1000;
        }

        override public function _ready():void {
            var bench:String = Launch.option("--bench", "");
            var scenario:Scenario = new Scenario(bench == "" ? "station" : bench, milliseconds);
            game = scenario.simulation;
            limit = bench == "" ? 0 : int(Launch.option("--ticks", String(scenario.ticks)));
            marsh = new MarshView();
            addChild(marsh);
            marsh.setup(game);
            station = new StationAudio();
            addChild(station);
            station.setup(game);
            effects = new Impressions();
            marsh.addChild(effects);
            effects.setup(game);
            desk = new Desk();
            addChild(desk);
            desk.setup(game);
            desk.field();
            if (limit == 0) {
                orders = new Orders();
                addChild(orders);
                orders.setup(game);
                watch = new Watch();
                addChild(watch);
                watch.setup(game, desk, orders, marsh, station);
            }
        }

        override public function _exitTree():void {
            if (watch != null) watch.close();
            if (desk != null) desk.close();
            if (marsh != null) marsh.close(game.events);
            if (effects != null) effects.close(game.events);
            if (station != null) station.close(game.events);
            if (game != null) game.stats.close(game.events);
        }

        override public function _process(delta:Number):void {
            var start:Number = milliseconds();
            var steps:int = stepper.advance(limit > 0 ? 1.0 / 60 : delta);
            if (watch != null) watch.poll();
            for (var i:int = 0; i < steps; i++) {
                if (watch == null) game.step();
                else watch.advance();
            }
            var drawn:Number = milliseconds();
            marsh.present(game);
            station.present(game);
            effects.shake = marsh.shake;
            effects.present(marsh);
            if (watch == null) desk.hud(game, 1, false);
            else watch.hud();
            var end:Number = milliseconds();
            presentation += end - drawn;
            total += end - start;
            if (limit == 0) return;
            if (game.tick % 60 == 0 || game.tick == limit) checks.push(game.checksum());
            if (game.tick < limit) return;
            var names:Array = ["input", "spawn", "flow", "ai", "movement", "spatial", "collision", "combat", "status", "pickups", "light", "economy", "events"];
            var systems:Object = {};
            for (var n:int = 0; n < names.length; n++) systems[names[n]] = game.elapsed[n];
            trace(JSON.stringify({total_ms:total, systems_ms:systems, presentation_ms:presentation,
                                  peak:game.pool.peak, built:game.stats.built, destroyed:game.stats.destroyed,
                                  deaths:game.stats.deaths, allocations:null, checksums:checks}));
            getTree().quit();
        }
    }
}
