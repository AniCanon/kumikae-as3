using System;
using System.Collections.Generic;
using System.Text.Json;
using Godot;

namespace Kumikae.LanternReach;

public sealed class Register {
    public List<LogEntry> entries = new();
    public int best;
    public int hand;
    public string saved = "";
    public double volume = 0.65;
    public bool shake = true;
    public bool accessible;
    private readonly string path;
    private readonly Rules rules;
    private readonly EventBus bus;
    private readonly Subscription listener;
    private static readonly JsonSerializerOptions Fields = new() { IncludeFields = true };

    public Register(Simulation game, string location) {
        rules = game.rules;
        path = location;
        bus = game.events;
        listener = bus.listen(record);
        load();
    }

    public void close() => bus.remove(listener);

    public void load() {
        if (!FileAccess.FileExists(path)) return;
        using var json = JsonDocument.Parse(FileAccess.GetFileAsString(path));
        var data = json.RootElement;
        entries = data.GetProperty("entries").Deserialize<List<LogEntry>>(Fields);
        best = data.GetProperty("best").GetInt32();
        hand = data.GetProperty("hand").GetInt32();
        saved = data.GetProperty("saved").GetString();
        volume = data.GetProperty("volume").GetDouble();
        shake = data.GetProperty("shake").GetBoolean();
        accessible = data.GetProperty("accessible").GetBoolean();
    }

    public void save() {
        var data = new { version = 1, entries, best, hand, saved, volume, shake, accessible };
        using var file = FileAccess.Open(path + ".tmp", FileAccess.ModeFlags.Write);
        if (file == null) throw new InvalidOperationException("Cannot write keeper register");
        bool stored = file.StoreString(JsonSerializer.Serialize(data, Fields));
        file.Close();
        if (!stored || DirAccess.RenameAbsolute(path + ".tmp", path) != Error.Ok)
            throw new InvalidOperationException("Cannot replace keeper register");
    }

    private void record(WorldEvent item) {
        if (item.kind != WorldEvent.DAWN && item.kind != WorldEvent.LOST_WATCH) return;
        entries.Add(new LogEntry { hand = hand, night = item.value, lost = item.kind == WorldEvent.LOST_WATCH,
            lesson = entries.Count % rules.knowledge.Count });
        best = Math.Max(best, item.value);
    }

    public bool knows(string subject, int value) {
        foreach (var page in entries) {
            var advice = rules.knowledge[page.lesson];
            if (advice.subject == subject && advice.value == value) return true;
        }
        return false;
    }
}
