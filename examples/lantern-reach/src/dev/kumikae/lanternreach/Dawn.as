package dev.kumikae.lanternreach {
    import dev.kumikae.lanternreach.sim.*;

    public final class Dawn {
        public static function write(game:Simulation, night:int):String {
            var player:Keeper = game.keeper;
            var state:Object = {night:night, tick:game.tick, seed:game.random.state,
                hero:{x:player.x, y:player.y, aimX:player.aimX, aimY:player.aimY, speed:player.speed, pull:player.pull, damage:player.damage, range:player.range, haste:player.haste, orbit:player.orbit, spark:player.spark, xp:player.xp, level:player.level, salvage:player.salvage},
                stats:{built:game.stats.built, destroyed:game.stats.destroyed, lost:game.stats.lost, waves:game.stats.waves, deaths:game.stats.deaths},
                beacons:[], ground:[], towers:[], offers:[]};
            for each (var choice:int in player.offers) state.offers.push(choice);
            for (var i:int = 0; i < game.grid.beacons.length; i++)
                state.beacons.push([game.grid.beacons[i], game.combat.beaconHealth[i]]);
            for (var j:int = 0; j < game.grid.marsh.length; j++) {
                if (game.grid.marsh[j]) state.ground.push(j);
            }
            for each (var s:Structure in game.town.placed)
                state.towers.push([s.kind, s.cell, s.tier, s.branch, s.hp, s.spent, s.cooldown]);
            return JSON.stringify(state);
        }

        public static function read(game:Simulation, saved:String):int {
            var state:Object = JSON.parse(saved);
            game.clearWatch();
            game.grid.beacons.length = 0;
            game.combat.beaconHealth.length = 0;
            for each (var beacon:Array in state.beacons) {
                game.grid.beacons.push(beacon[0]);
                game.combat.beaconHealth.push(beacon[1]);
            }
            game.town.placed.length = 0;
            for (var i:int = 0; i < game.grid.marsh.length; i++) {
                game.grid.marsh[i] = false;
                game.grid.walls[i] = false;
                game.town.at[i] = null;
            }
            for each (var tile:int in state.ground) game.grid.marsh[tile] = true;
            for each (var row:Array in state.towers) {
                var s:Structure = new Structure();
                s.kind = row[0];
                s.cell = row[1];
                s.tier = row[2];
                s.branch = row[3];
                s.hp = row[4];
                s.spent = row[5];
                s.cooldown = row[6];
                game.town.restore(s);
            }
            game.keeper.offers.length = 0;
            for each (var choice:int in state.offers) game.keeper.offers.push(choice);
            game.keeper.x = state.hero.x;
            game.keeper.y = state.hero.y;
            game.keeper.aimX = state.hero.aimX;
            game.keeper.aimY = state.hero.aimY;
            game.keeper.speed = state.hero.speed;
            game.keeper.pull = state.hero.pull;
            game.keeper.damage = state.hero.damage;
            game.keeper.range = state.hero.range;
            game.keeper.haste = state.hero.haste;
            game.keeper.orbit = state.hero.orbit;
            game.keeper.spark = state.hero.spark;
            game.keeper.xp = state.hero.xp;
            game.keeper.level = state.hero.level;
            game.keeper.salvage = state.hero.salvage;
            game.stats.built = state.stats.built;
            game.stats.destroyed = state.stats.destroyed;
            game.stats.lost = state.stats.lost;
            game.stats.waves = state.stats.waves;
            game.stats.deaths = state.stats.deaths;
            game.random.state = state.seed;
            game.tick = state.tick;
            game.grid.revision++;
            game.flow.update();
            game.light.update();
            return state.night;
        }
    }
}
