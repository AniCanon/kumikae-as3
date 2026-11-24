package dev.kumikae.lanternreach.sim {
    public final class TownGrid {
        public var width:int;
        public var height:int;
        public var walls:Vector.<Boolean> = new Vector.<Boolean>();
        public var marsh:Vector.<Boolean> = new Vector.<Boolean>();
        public var beacons:Vector.<int> = new Vector.<int>();
        public var revision:int = 0;

        public function TownGrid(columns:int, rows:int) {
            width = columns;
            height = rows;
            for (var i:int = 0; i < width * height; i++) {
                walls.push(false);
                marsh.push(false);
            }
        }

        public function cell(x:Number, y:Number):int {
            return int(Math.max(0, Math.min(height - 1, y))) * width +
                int(Math.max(0, Math.min(width - 1, x)));
        }

        public function neighbour(cellId:int, direction:int):int {
            if (direction == 0) return cellId >= width ? cellId - width : -1;
            if (direction == 1) return cellId % width > 0 ? cellId - 1 : -1;
            if (direction == 2) return cellId % width < width - 1 ? cellId + 1 : -1;
            return cellId + width < walls.length ? cellId + width : -1;
        }
    }
}
