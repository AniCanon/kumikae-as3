using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class InputSystem {
    private TownGrid grid;
    private Keeper keeper;
    private CommandQueue commands;
    private FlowField flow;

    public InputSystem(TownGrid map, Keeper player, CommandQueue queue, FlowField paths) {
        grid = map;
        keeper = player;
        commands = queue;
        flow = paths;
    }

    public void update(int tick) {
        TickCommand command = commands.take(tick);
        while (command != null) {
            if (command.action == "move") {
                keeper.dx = command.x;
                keeper.dy = command.y;
            }
            if (command.action == "wall") {
                int id = grid.cell(command.x, command.y);
                if (!grid.walls[id] && keeper.salvage >= 4) {
                    grid.walls[id] = true;
                    grid.revision++;
                    keeper.salvage -= 4;
                }
            }
            if (command.action == "sell") {
                int sold = grid.cell(command.x, command.y);
                if (grid.walls[sold]) {
                    grid.walls[sold] = false;
                    grid.revision++;
                    flow.opened(sold);
                    keeper.salvage += 2;
                }
            }
            command = commands.take(tick);
        }
    }
}
