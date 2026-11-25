package dev.kumikae.lanternreach.sim {
    public final class LightField {
        public var level:Vector.<int> = new Vector.<int>();
        public var daylight:int;
        private var grid:TownGrid;
        private var buildings:Buildings;
        private var pool:CreaturePool;
        private var rules:Rules;

        public function LightField(map:TownGrid, town:Buildings, creatures:CreaturePool, definitions:Rules) {
            grid = map;
            buildings = town;
            pool = creatures;
            rules = definitions;
            for (var i:int = 0; i < grid.walls.length; i++) level.push(0);
        }

        public function update():void {
            for (var i:int = 0; i < level.length; i++) level[i] = daylight;
            for each (var id:int in grid.beacons) shine(id, 5);
            for each (var tower:Structure in buildings.placed) {
                if (tower.kind == Structure.LAMP || tower.kind == Structure.BRAZIER) {
                    shine(tower.cell, rules.structures[tower.kind].radius + tower.tier + (tower.branch == 1 ? 2 : 0));
                }
            }
            for each (var fire:Creature in pool.slots) {
                if (fire.active && fire.burn > 0) shine(grid.cell(fire.x, fire.y), 1);
            }
            for each (var enemy:Creature in pool.slots) {
                if (enemy.active) enemy.lit = level[grid.cell(enemy.x, enemy.y)] > 0;
            }
        }

        private function shine(origin:int, radius:int):void {
            var cx:int = origin % grid.width;
            var cy:int = int(origin / grid.width);
            for (var y:int = Math.max(0, cy - radius); y <= Math.min(grid.height - 1, cy + radius); y++) {
                for (var x:int = Math.max(0, cx - radius); x <= Math.min(grid.width - 1, cx + radius); x++) {
                    var dx:int = x - cx;
                    var dy:int = y - cy;
                    var squared:int = dx * dx + dy * dy;
                    if (squared > radius * radius) continue;
                    var steps:int = Math.max(Math.abs(dx), Math.abs(dy));
                    var clear:Boolean = true;
                    for (var j:int = 1; j < steps; j++) {
                        if (grid.walls[grid.cell(cx + 0.5 + dx * j / Number(steps), cy + 0.5 + dy * j / Number(steps))]) {
                            clear = false;
                            break;
                        }
                    }
                    var tile:int = y * grid.width + x;
                    if (clear) level[tile] = Math.max(level[tile], 255 - int(220 * squared / (radius * radius)));
                }
            }
        }
    }
}
