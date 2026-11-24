package dev.kumikae.lanternreach.sim {
    public final class BuildRoutes {
        private var grid:TownGrid;
        private var visited:Vector.<int> = new Vector.<int>();
        private var frontier:Vector.<int> = new Vector.<int>();
        private var generation:int;

        public function BuildRoutes(map:TownGrid) {
            grid = map;
            for (var i:int = 0; i < map.walls.length; i++) visited.push(0);
        }

        public function permits(block:int):Boolean {
            generation++;
            frontier.length = 0;
            for (var x:int = 0; x < grid.width; x++) visit(x, block);
            for (var i:int = 0; i < frontier.length; i++) {
                for (var d:int = 0; d < 4; d++) visit(grid.neighbour(frontier[i], d), block);
            }
            for (var b:int = 0; b < grid.beacons.length; b++) {
                if (visited[grid.beacons[b]] != generation) return false;
            }
            return true;
        }

        private function visit(id:int, block:int):void {
            if (id < 0 || id == block || grid.walls[id] || visited[id] == generation) return;
            visited[id] = generation;
            frontier.push(id);
        }
    }
}
