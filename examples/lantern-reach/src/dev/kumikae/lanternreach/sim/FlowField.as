package dev.kumikae.lanternreach.sim {
    public final class FlowField {
        public static const UNREACHABLE:int = 2147483647;
        public var cost:Vector.<int> = new Vector.<int>();
        public var toward:Vector.<int> = new Vector.<int>();
        private var grid:TownGrid;
        private var buckets:Vector.<FlowBucket> = new Vector.<FlowBucket>();
        private var builtRevision:int = -1;

        public function FlowField(map:TownGrid) {
            grid = map;
            for (var i:int = 0; i < grid.walls.length; i++) {
                cost.push(UNREACHABLE);
                toward.push(-1);
            }
            for (var j:int = 0; j < 4; j++) buckets.push(new FlowBucket());
        }

        public function update():void {
            if (builtRevision != grid.revision) rebuild();
        }

        public function rebuild():void {
            for (var i:int = 0; i < cost.length; i++) {
                cost[i] = UNREACHABLE;
                toward[i] = -1;
            }
            for (var j:int = 0; j < grid.beacons.length; j++) {
                var source:int = grid.beacons[j];
                cost[source] = 0;
                toward[source] = source;
                buckets[0].cells.push(source);
            }
            propagate(0, grid.beacons.length);
        }

        // Opening a wall only lowers costs. Seed the four reachable neighbours.
        public function opened(cellId:int):void {
            var best:int = UNREACHABLE;
            var parent:int = -1;
            for (var d:int = 0; d < 4; d++) {
                var n:int = grid.neighbour(cellId, d);
                if (n >= 0 && cost[n] < best) {
                    parent = n;
                    best = cost[n];
                }
            }
            if (parent >= 0) {
                best += grid.marsh[cellId] ? 3 : 1;
                cost[cellId] = best;
                toward[cellId] = parent;
                buckets[best % 4].cells.push(cellId);
                propagate(best, 1);
            }
        }

        private function propagate(distance:int, pending:int):void {
            while (pending > 0) {
                var bucket:FlowBucket = buckets[distance % 4];
                for (var i:int = 0; i < bucket.cells.length; i++) {
                    var from:int = bucket.cells[i];
                    pending--;
                    if (cost[from] != distance) continue;
                    for (var d:int = 0; d < 4; d++) {
                        var n:int = grid.neighbour(from, d);
                        if (n < 0 || grid.walls[n]) continue;
                        var value:int = distance + (grid.marsh[n] ? 3 : 1);
                        if (value >= cost[n]) continue;
                        cost[n] = value;
                        toward[n] = from;
                        buckets[value % 4].cells.push(n);
                        pending++;
                    }
                }
                bucket.cells.length = 0;
                distance++;
            }
            builtRevision = grid.revision;
        }
    }
}
