using System.Collections.Generic;
using System.Text.Json;

namespace Kumikae.LanternReach;

public static class Dawn {
    public static string write(Simulation game, int night) {
        var player = game.keeper;
        var beacons = new List<int[]>();
        for (int i = 0; i < game.grid.beacons.Count; i++)
            beacons.Add(new[] { game.grid.beacons[i], game.combat.beaconHealth[i] });
        var ground = new List<int>();
        for (int i = 0; i < game.grid.marsh.Count; i++)
            if (game.grid.marsh[i]) ground.Add(i);
        var towers = new List<int[]>();
        foreach (var s in game.town.placed)
            towers.Add(new[] { s.kind, s.cell, s.tier, s.branch, s.hp, s.spent, s.cooldown });
        return JsonSerializer.Serialize(new { night, game.tick, seed = game.random.state,
            hero = new { player.x, player.y, player.aimX, player.aimY, player.speed, player.pull, player.damage, player.range, player.haste, player.orbit, player.spark, player.xp, player.level, player.salvage },
            stats = new { game.stats.built, game.stats.destroyed, game.stats.lost, game.stats.waves, game.stats.deaths },
            beacons, ground, towers, shore = game.wrack, offers = player.offers });
    }

    public static int read(Simulation game, string saved) {
        using var json = JsonDocument.Parse(saved);
        var state = json.RootElement;
        game.clearWatch();
        game.grid.beacons.Clear();
        game.combat.beaconHealth.Clear();
        foreach (var row in state.GetProperty("beacons").EnumerateArray()) {
            game.grid.beacons.Add(row[0].GetInt32());
            game.combat.beaconHealth.Add(row[1].GetInt32());
        }
        game.town.placed.Clear();
        for (int i = 0; i < game.grid.marsh.Count; i++) {
            game.grid.marsh[i] = false;
            game.grid.walls[i] = false;
            game.town.at[i] = null;
        }
        foreach (var tile in state.GetProperty("ground").EnumerateArray()) game.grid.marsh[tile.GetInt32()] = true;
        foreach (var row in state.GetProperty("towers").EnumerateArray())
            game.town.restore(new Structure {
                kind = row[0].GetInt32(),
                cell = row[1].GetInt32(),
                tier = row[2].GetInt32(),
                branch = row[3].GetInt32(),
                hp = row[4].GetInt32(),
                spent = row[5].GetInt32(),
                cooldown = row[6].GetInt32(),
            });
        game.wrack.Clear();
        if (state.TryGetProperty("shore", out var shore)) {
            foreach (var tile in shore.EnumerateArray()) game.wrack.Add(tile.GetInt32());
        } else {
            game.wrack.AddRange(game.rules.nights[state.GetProperty("night").GetInt32() - 1].shore);
        }
        game.keeper.offers.Clear();
        foreach (var choice in state.GetProperty("offers").EnumerateArray()) game.keeper.offers.Add(choice.GetInt32());
        var hero = state.GetProperty("hero");
        game.keeper.x = hero.GetProperty("x").GetDouble();
        game.keeper.y = hero.GetProperty("y").GetDouble();
        game.keeper.aimX = hero.GetProperty("aimX").GetDouble();
        game.keeper.aimY = hero.GetProperty("aimY").GetDouble();
        game.keeper.speed = hero.GetProperty("speed").GetDouble();
        game.keeper.pull = hero.GetProperty("pull").GetDouble();
        game.keeper.damage = hero.GetProperty("damage").GetInt32();
        game.keeper.range = hero.GetProperty("range").GetInt32();
        game.keeper.haste = hero.GetProperty("haste").GetInt32();
        game.keeper.orbit = hero.GetProperty("orbit").GetInt32();
        game.keeper.spark = hero.GetProperty("spark").GetInt32();
        game.keeper.xp = hero.GetProperty("xp").GetInt32();
        game.keeper.level = hero.GetProperty("level").GetInt32();
        game.keeper.salvage = hero.GetProperty("salvage").GetInt32();
        var stats = state.GetProperty("stats");
        game.stats.built = stats.GetProperty("built").GetInt32();
        game.stats.destroyed = stats.GetProperty("destroyed").GetInt32();
        game.stats.lost = stats.GetProperty("lost").GetInt32();
        game.stats.waves = stats.GetProperty("waves").GetInt32();
        game.stats.deaths = stats.GetProperty("deaths").GetInt32();
        game.random.state = state.GetProperty("seed").GetUInt32();
        game.tick = state.GetProperty("tick").GetInt32();
        game.grid.revision++;
        game.flow.update();
        game.light.update();
        return state.GetProperty("night").GetInt32();
    }
}
