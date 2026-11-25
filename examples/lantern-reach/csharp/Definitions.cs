using System.Text.Json;
using Godot;

namespace Kumikae.LanternReach;

public static class Definitions
{
    private static JsonDocument Read(string name) =>
        JsonDocument.Parse(FileAccess.GetFileAsString($"res://data/{name}.json"));

    public static Rules Load()
    {
        var rules = new Rules();
        using var enemies = Read("enemies");
        foreach (var row in enemies.RootElement.EnumerateArray())
            rules.enemies.Add(new EnemyDef
            {
                key = row.GetProperty("key").GetString(),
                health = row.GetProperty("health").GetInt32(),
                speed = row.GetProperty("speed").GetDouble(),
                damage = row.GetProperty("damage").GetInt32(),
                xp = row.GetProperty("xp").GetInt32(),
                period = row.GetProperty("period").GetInt32(),
            });
        using var structures = Read("structures");
        foreach (var row in structures.RootElement.EnumerateArray())
            rules.structures.Add(new StructureDef
            {
                key = row.GetProperty("key").GetString(),
                cost = row.GetProperty("cost").GetInt32(),
                health = row.GetProperty("health").GetInt32(),
                radius = row.GetProperty("radius").GetInt32(),
                power = row.GetProperty("power").GetInt32(),
                period = row.GetProperty("period").GetInt32(),
                branchA = row.GetProperty("branchA").GetString(),
                branchB = row.GetProperty("branchB").GetString(),
            });
        using var weapons = Read("weapons");
        foreach (var row in weapons.RootElement.EnumerateArray())
            rules.weapons.Add(new WeaponDef
            {
                key = row.GetProperty("key").GetString(),
                damage = row.GetProperty("damage").GetInt32(),
                range = row.GetProperty("range").GetInt32(),
                period = row.GetProperty("period").GetInt32(),
                pierce = row.GetProperty("pierce").GetInt32(),
            });
        using var upgrades = Read("upgrades");
        foreach (var row in upgrades.RootElement.EnumerateArray())
            rules.upgrades.Add(new UpgradeDef
            {
                key = row.GetProperty("key").GetString(),
                stat = row.GetProperty("stat").GetString(),
                amount = row.GetProperty("amount").GetDouble(),
                weight = row.GetProperty("weight").GetInt32(),
            });
        using var knowledge = Read("knowledge");
        foreach (var row in knowledge.RootElement.EnumerateArray())
            rules.knowledge.Add(new Advice {
                key = row.GetProperty("key").GetString(), subject = row.GetProperty("subject").GetString(),
                value = row.GetProperty("value").GetInt32()
            });
        using var nights = Read("nights");
        foreach (var row in nights.RootElement.EnumerateArray()) {
            var plan = new NightPlan { duration = row.GetProperty("duration").GetInt32() };
            foreach (var tile in row.GetProperty("shore").EnumerateArray()) plan.shore.Add(tile.GetInt32());
            foreach (var g in row.GetProperty("waves").EnumerateArray())
                plan.waves.Add(new Wave { tick = g[0].GetInt32(), kind = g[1].GetInt32(),
                    count = g[2].GetInt32(), depth = g[3].GetInt32(), name = g[4].GetInt32() });
            rules.nights.Add(plan);
        }
        return rules;
    }
}
