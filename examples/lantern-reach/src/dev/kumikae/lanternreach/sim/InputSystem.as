package dev.kumikae.lanternreach.sim {
    public final class InputSystem {
        private var grid:TownGrid;
        private var keeper:Keeper;
        private var commands:CommandQueue;
        private var town:Buildings;
        private var levels:LevelUps;
        private var rules:Rules;

        public function InputSystem(map:TownGrid, player:Keeper, queue:CommandQueue, buildings:Buildings, upgrades:LevelUps, defs:Rules) {
            grid = map;
            keeper = player;
            commands = queue;
            town = buildings;
            levels = upgrades;
            rules = defs;
        }

        public function update(tick:int):void {
            var command:TickCommand = commands.take(tick);
            while (command != null) {
                apply(command);
                command = commands.take(tick);
            }
        }

        private function apply(command:TickCommand):void {
            var cell:int = grid.cell(command.x, command.y);
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
                    levels.choose(int(command.x));
                    break;
                default:
                    for (var i:int = 0; i < rules.structures.length; i++) {
                        if (rules.structures[i].key == command.action) town.build(cell, i);
                    }
                    break;
            }
        }
    }
}
