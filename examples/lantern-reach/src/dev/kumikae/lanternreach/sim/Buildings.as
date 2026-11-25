package dev.kumikae.lanternreach.sim {
    public final class Buildings {
        public var placed:Vector.<Structure> = new Vector.<Structure>();
        public var at:Vector.<Structure> = new Vector.<Structure>();
        private var grid:TownGrid;
        private var rules:Rules;
        private var player:Keeper;
        private var routes:BuildRoutes;
        private var flow:FlowField;
        private var events:EventBus;

        public function Buildings(map:TownGrid, definitions:Rules, keeper:Keeper, paths:FlowField, bus:EventBus) {
            grid = map;
            rules = definitions;
            player = keeper;
            flow = paths;
            events = bus;
            routes = new BuildRoutes(grid);
            for (var i:int = 0; i < grid.walls.length; i++) at.push(null);
        }

        public function build(cell:int, kind:int):Boolean {
            var def:StructureDef = rules.structures[kind];
            if (at[cell] != null || grid.beacons.indexOf(cell) >= 0 || player.salvage < def.cost ||
                    (kind == Structure.WALL && !routes.permits(cell))) {
                events.post(WorldEvent.REFUSED, cell, kind);
                return false;
            }
            var tower:Structure = new Structure();
            tower.kind = kind;
            tower.cell = cell;
            tower.hp = def.health;
            tower.spent = def.cost;
            tower.policy = new Nearest();
            player.salvage -= def.cost;
            at[cell] = tower;
            placed.push(tower);
            if (kind == Structure.WALL) {
                grid.walls[cell] = true;
                grid.revision++;
            }
            events.post(WorldEvent.BUILT, cell, kind);
            return true;
        }

        public function improve(cell:int, branch:int):void {
            var tower:Structure = at[cell];
            if (tower == null || tower.tier >= 3) return;
            var def:StructureDef = rules.structures[tower.kind];
            if (player.salvage < def.cost || (tower.tier == 2 && branch != 1 && branch != 2)) {
                events.post(WorldEvent.REFUSED, cell, tower.kind);
                return;
            }
            player.salvage -= def.cost;
            tower.spent += def.cost;
            tower.tier++;
            tower.hp += def.health * (tower.tier == 3 && branch == 1 ? 2 : 1);
            if (tower.tier == 3) {
                tower.branch = branch;
                if (branch == 1) tower.policy = new Strongest();
                else tower.policy = new FirstAlongPath();
            }
        }

        public function remove(cell:int, sell:Boolean):void {
            var tower:Structure = at[cell];
            if (tower == null) return;
            if (sell) player.salvage += int(tower.spent / 2);
            if (tower.kind == Structure.WALL) {
                flow.update();
                grid.walls[cell] = false;
                grid.revision++;
                flow.opened(cell);
            }
            at[cell] = null;
            placed.splice(placed.indexOf(tower), 1);
            events.post(WorldEvent.DESTROYED, cell, tower.kind);
        }
        public function restore(tower:Structure):void {
            tower.policy = tower.branch == 1 ? new Strongest() : tower.branch == 2 ? new FirstAlongPath() : new Nearest();
            placed.push(tower);
            at[tower.cell] = tower;
            if (tower.kind == Structure.WALL) grid.walls[tower.cell] = true;
        }

    }
}
