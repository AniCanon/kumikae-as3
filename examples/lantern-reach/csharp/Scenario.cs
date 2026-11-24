using System;
using System.Text.Json;
using Godot;

namespace Kumikae.LanternReach;

public sealed class Scenario
{
    public Simulation simulation;
    public int ticks;

    public Scenario(string name, Func<double> timer)
    {
        using var json = JsonDocument.Parse(FileAccess.GetFileAsString($"res://data/scenarios/{name}.json"));
        var data = json.RootElement;
        var grid = new TownGrid(data.GetProperty("width").GetInt32(), data.GetProperty("height").GetInt32());
        foreach (var id in data.GetProperty("beacons").EnumerateArray()) grid.beacons.Add(id.GetInt32());
        foreach (var patch in data.GetProperty("marsh").EnumerateArray())
        {
            for (int y = patch[1].GetInt32(); y < patch[1].GetInt32() + patch[3].GetInt32(); y++)
                for (int x = patch[0].GetInt32(); x < patch[0].GetInt32() + patch[2].GetInt32(); x++)
                    grid.marsh[grid.cell(x, y)] = true;
        }
        simulation = new Simulation(grid, data.GetProperty("seed").GetUInt32(), data.GetProperty("population").GetInt32(), timer);
        foreach (var row in data.GetProperty("commands").EnumerateArray())
            simulation.commands.enqueue(new TickCommand(row[0].GetInt32(), row[1].GetString(), row[2].GetDouble(), row[3].GetDouble()));
        ticks = data.GetProperty("ticks").GetInt32();
    }
}
