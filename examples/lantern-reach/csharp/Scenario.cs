using System;
using System.Text.Json;
using Godot;

namespace Kumikae.LanternReach;

public sealed class Scenario
{
    public Simulation simulation;
    public int ticks;
    public System.Collections.Generic.List<string> menu = new();

    public Scenario(string name, Func<double> timer)
    {
        using var json = JsonDocument.Parse(FileAccess.GetFileAsString("res://data/" + (name == "station" ? name : "scenarios/" + name) + ".json"));
        var data = json.RootElement;
        var grid = new TownGrid(data.GetProperty("width").GetInt32(), data.GetProperty("height").GetInt32());
        foreach (var id in data.GetProperty("beacons").EnumerateArray()) grid.beacons.Add(id.GetInt32());
        foreach (var patch in data.GetProperty("marsh").EnumerateArray())
        {
            for (int y = patch[1].GetInt32(); y < patch[1].GetInt32() + patch[3].GetInt32(); y++)
                for (int x = patch[0].GetInt32(); x < patch[0].GetInt32() + patch[2].GetInt32(); x++)
                    grid.marsh[grid.cell(x, y)] = true;
        }
        simulation = new Simulation(grid, data.GetProperty("seed").GetUInt32(), Definitions.Load(), timer);
        simulation.keeper.salvage = data.GetProperty("salvage").GetInt32();
        foreach (var group in data.GetProperty("waves").EnumerateArray())
            simulation.spawn.waves.Add(new Wave {
                tick = group[0].GetInt32(), kind = group[1].GetInt32(), count = group[2].GetInt32(),
                depth = group[3].GetInt32(), name = group[4].GetInt32()
            });
        foreach (var row in data.GetProperty("commands").EnumerateArray())
            simulation.commands.enqueue(new TickCommand(row[0].GetInt32(), row[1].GetString(), row[2].GetDouble(), row[3].GetDouble()));
        if (data.TryGetProperty("actors", out var actors)) {
            foreach (var actor in actors.EnumerateArray()) {
                var resident = simulation.spawn.place(actor[0].GetInt32(), actor[1].GetDouble(), actor[2].GetDouble(), actor[3].GetInt32());
                resident.hp = actor[4].GetInt32();
                resident.cooldown = actor[5].GetInt32();
            }
        }
        if (data.TryGetProperty("menu", out var actions)) {
            foreach (var action in actions.EnumerateArray()) menu.Add(action.GetString());
        }
        ticks = data.GetProperty("ticks").GetInt32();
    }
}
