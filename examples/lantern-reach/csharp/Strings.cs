using Godot;
using System.Collections.Generic;
using System.Text.Json;

namespace Kumikae.LanternReach;

public sealed class Strings {
    private readonly Dictionary<string, string> entries =
        JsonSerializer.Deserialize<Dictionary<string, string>>(FileAccess.GetFileAsString("res://data/strings.json"));

    public string get(string key) => entries[key];
}
