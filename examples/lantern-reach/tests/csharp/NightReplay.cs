using Godot;
using System;
using System.Collections.Generic;
using System.Text.Json;

namespace Kumikae.LanternReach.Tests;

public partial class NightReplay : Node {
    public override void _Ready() {
        try { new SystemCases().run(); replay(); }
        catch (Exception error) { GD.PushError(error.ToString()); GetTree().Quit(1); }
    }

    private void replay() {
        var g = new Scenario("station", () => 0).simulation;
        var tides = new Tides(g);
        tides.morning();
        var book = new Register(g, "user://probe-" + OS.GetProcessId() + ".json");
        using var rows = JsonDocument.Parse(FileAccess.GetFileAsString("res://tests/first-watch.json"));
        foreach (var command in rows.RootElement.EnumerateArray())
            g.commands.enqueue(new TickCommand(command[0].GetInt32(), command[1].GetString(),
                command[2].GetDouble(), command[3].GetDouble()));
        tides.daylight();
        tides.dusk();
        var checks = new List<string>();
        while (tides.phase == "night") {
            tides.advance();
            if (g.tick % 60 == 0) checks.Add(g.checksum());
        }
        checks.Add(g.checksum());
        string snapshot = Dawn.write(g, tides.number);
        book.saved = snapshot;
        book.save();
        book.load();
        var other = new Scenario("station", () => 0).simulation;
        int restored = Dawn.read(other, book.saved);
        GD.Print(JsonSerializer.Serialize(new { checksums = checks, night = restored, lit = g.grid.beacons.Count,
            g.tick, entries = book.entries.Count, unlocked = book.knows("structure", 3), restored = other.checksum() }));
        book.close();
        g.stats.close(g.events);
        other.stats.close(other.events);
        DirAccess.RemoveAbsolute("user://probe-" + OS.GetProcessId() + ".json");
        GetTree().Quit(restored == 2 && g.checksum() == other.checksum() ? 0 : 1);
    }
}
