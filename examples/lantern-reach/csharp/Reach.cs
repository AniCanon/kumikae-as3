using System;
using System.Collections.Generic;
using System.Text.Json;
using Godot;

namespace Kumikae.LanternReach;

public partial class Reach : Node2D
{
    private Simulation game;
    private readonly FixedStep stepper = new();
    private MarshView marsh;
    private StationAudio station;
    private Impressions effects;
    private Desk desk;
    private Orders orders;
    private Watch watch;
    private int limit;
    private readonly List<string> checks = new();
    private double total;
    private double presentation;
    private long allocated;

    private static double Milliseconds() => Time.GetTicksUsec() / 1000.0;

    private static string Option(string key, string fallback)
    {
        var args = OS.GetCmdlineUserArgs();
        int index = Array.IndexOf(args, key);
        return index >= 0 && index + 1 < args.Length ? args[index + 1] : fallback;
    }

    public override void _Ready()
    {
        string bench = Option("--bench", "");
        var scenario = new Scenario(bench == "" ? "station" : bench, Milliseconds);
        game = scenario.simulation;
        limit = bench == "" ? 0 : int.Parse(Option("--ticks", scenario.ticks.ToString()));
        marsh = new MarshView();
        AddChild(marsh);
        marsh.setup(game);
        station = new StationAudio();
        AddChild(station);
        station.setup(game);
        effects = new Impressions();
        marsh.AddChild(effects);
        effects.setup(game);
        desk = new Desk();
        AddChild(desk);
        desk.setup(game);
        desk.field();
        if (limit == 0) {
            orders = new Orders();
            AddChild(orders);
            orders.setup(game);
            watch = new Watch();
            AddChild(watch);
            watch.setup(game, desk, orders, marsh, station);
        }
    }

    public override void _ExitTree()
    {
        if (watch != null) watch.close();
        if (desk != null) desk.close();
        if (marsh != null) marsh.close(game.events);
        if (effects != null) effects.close(game.events);
        if (station != null) station.close(game.events);
        if (game != null) game.stats.close(game.events);
    }

    public override void _Process(double delta)
    {
        long before = GC.GetAllocatedBytesForCurrentThread();
        double start = Milliseconds();
        int steps = stepper.advance(limit > 0 ? 1.0 / 60 : delta);
        if (watch != null) watch.poll();
        for (int i = 0; i < steps; i++) {
            if (watch == null) game.step();
            else watch.advance();
        }
        double drawn = Milliseconds();
        marsh.present(game);
        station.present(game);
        effects.shake = marsh.shake;
        effects.present(marsh);
        if (watch == null) desk.hud(game, 1, false);
        else watch.hud();
        double end = Milliseconds();
        presentation += end - drawn;
        total += end - start;
        allocated += GC.GetAllocatedBytesForCurrentThread() - before;
        if (limit == 0) return;
        if (game.tick % 60 == 0 || game.tick == limit) checks.Add(game.checksum());
        if (game.tick < limit) return;
        string[] names = { "input", "spawn", "flow", "ai", "movement", "spatial", "collision", "combat", "status", "pickups", "light", "economy", "events" };
        var systems = new Dictionary<string, double>();
        for (int n = 0; n < names.Length; n++) systems[names[n]] = game.elapsed[n];
        GD.Print(JsonSerializer.Serialize(new { total_ms = total, systems_ms = systems,
            presentation_ms = presentation, peak = game.pool.peak, built = game.stats.built, destroyed = game.stats.destroyed,
            deaths = game.stats.deaths, allocations = allocated, checksums = checks }));
        GetTree().Quit();
    }
}
