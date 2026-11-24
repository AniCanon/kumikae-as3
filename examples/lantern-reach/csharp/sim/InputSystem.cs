using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class InputSystem {
    private TownGrid grid;
    private Keeper keeper;
    private CommandQueue commands;
    private Buildings town;
    private LevelUps levels;
    private Rules rules;

    public InputSystem(TownGrid map, Keeper player, CommandQueue queue, Buildings buildings, LevelUps upgrades, Rules defs) {
        grid = map;
        keeper = player;
        commands = queue;
        town = buildings;
        levels = upgrades;
        rules = defs;
    }

    public void update(int tick) {
        TickCommand command = commands.take(tick);
        while (command != null) {
            apply(command);
            command = commands.take(tick);
        }
    }

    private void apply(TickCommand command) {
        int cell = grid.cell(command.x, command.y);
        switch (command.action) {
            case "move":
                keeper.dx = command.x;
                keeper.dy = command.y;
                break;
            case "aim":
                keeper.aimX = command.x;
                keeper.aimY = command.y;
                break;
            case "fire":
                keeper.firing = command.x != 0;
                break;
            case "dash":
                if (keeper.recharge == 0) {
                    keeper.dash = 12;
                    keeper.recharge = 60;
                }
                break;
            case "sell":
                town.remove(cell, true);
                break;
            case "upgrade":
            case "branchA":
            case "branchB":
                town.improve(cell, command.action == "branchA" ? 1 : command.action == "branchB" ? 2 : 0);
                break;
            case "choose":
                levels.choose((int)(command.x));
                break;
            default:
                for (int i = 0; i < rules.structures.Count; i++) {
                    if (rules.structures[i].key == command.action) town.build(cell, i);
                }
                break;
        }
    }
}
