using System;
using System.Collections.Generic;
using System.Text.Json;
using Godot;

namespace Kumikae.LanternReach;

public partial class Reach : Node2D
{
    private Simulation game;
    private readonly FixedStep stepper = new();
    private Crowd crowd;
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
        var scenario = new Scenario(bench == "" ? "calm" : bench, Milliseconds);
        game = scenario.simulation;
        limit = bench == "" ? 0 : int.Parse(Option("--ticks", scenario.ticks.ToString()));
        crowd = new Crowd();
        AddChild(crowd);
    }

    public override void _ExitTree()
    {
        if (game != null) game.stats.close(game.events);
    }

    public override void _Process(double delta)
    {
        long before = GC.GetAllocatedBytesForCurrentThread();
        double start = Milliseconds();
        int steps = stepper.advance(limit > 0 ? 1.0 / 60 : delta);
        for (int i = 0; i < steps; i++) game.step();
        double drawn = Milliseconds();
        crowd.Present(game.pool);
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
        SetProcess(false);
        GetTree().Quit();
    }
}
