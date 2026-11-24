package dev.kumikae.lanternreach.sim {
    public final class InputSystem {
        private var grid:TownGrid;
        private var keeper:Keeper;
        private var commands:CommandQueue;
        private var flow:FlowField;

        public function InputSystem(map:TownGrid, player:Keeper, queue:CommandQueue, paths:FlowField) {
            grid = map;
            keeper = player;
            commands = queue;
            flow = paths;
        }

        public function update(tick:int):void {
            var command:TickCommand = commands.take(tick);
            while (command != null) {
                if (command.action == "move") {
                    keeper.dx = command.x;
                    keeper.dy = command.y;
                }
                if (command.action == "wall") {
                    var id:int = grid.cell(command.x, command.y);
                    if (!grid.walls[id] && keeper.salvage >= 4) {
                        grid.walls[id] = true;
                        grid.revision++;
                        keeper.salvage -= 4;
                    }
                }
                if (command.action == "sell") {
                    var sold:int = grid.cell(command.x, command.y);
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
}
