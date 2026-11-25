package dev.kumikae.lanternreach.sim {
    public final class PickupSystem {
        public var shards:Vector.<Shard> = new Vector.<Shard>();
        private var head:Vector.<int> = new Vector.<int>();
        private var free:Vector.<int> = new Vector.<int>();
        private var grid:TownGrid;
        private var player:Keeper;
        private var upgrades:LevelUps;

        public function PickupSystem(map:TownGrid, keeper:Keeper, levels:LevelUps) {
            grid = map;
            player = keeper;
            upgrades = levels;
            for (var i:int = 0; i < map.walls.length; i++) head.push(-1);
        }

        public function drop(x:Number, y:Number, value:int):void {
            var id:int = shards.length;
            if (free.length > 0) {
                id = free.pop();
            } else {
                shards.push(new Shard());
            }
            var shard:Shard = shards[id];
            shard.x = x;
            shard.y = y;
            shard.value = value;
            insert(id);
        }

        private function insert(id:int):void {
            var shard:Shard = shards[id];
            shard.cell = grid.cell(shard.x, shard.y);
            shard.next = head[shard.cell];
            head[shard.cell] = id;
        }

        public function update():void {
            var radius:Number = player.pull;
            var moved:Vector.<int> = new Vector.<int>();
            for (var y:int = int(Math.max(0, player.y - radius)); y <= Math.min(grid.height - 1, player.y + radius); y++) {
                for (var x:int = int(Math.max(0, player.x - radius)); x <= Math.min(grid.width - 1, player.x + radius); x++) {
                    var cell:int = y * grid.width + x;
                    var prev:int = -1;
                    var id:int = head[cell];
                    while (id >= 0) {
                        var shard:Shard = shards[id];
                        var next:int = shard.next;
                        var dx:Number = player.x - shard.x;
                        var dy:Number = player.y - shard.y;
                        var d:Number = dx * dx + dy * dy;
                        if (d < 0.25) {
                            player.xp += shard.value;
                            player.salvage++;
                            shard.value = 0;
                            free.push(id);
                        } else if (d <= radius * radius) {
                            shard.x += dx * 0.12;
                            shard.y += dy * 0.12;
                        }
                        if (shard.value == 0 || grid.cell(shard.x, shard.y) != cell) {
                            if (prev < 0) head[cell] = next;
                            else shards[prev].next = next;
                            if (shard.value > 0) moved.push(id);
                        } else prev = id;
                        id = next;
                    }
                }
            }
            for each (var changed:int in moved) insert(changed);
            upgrades.offer();
        }
        public function clear():void {
            shards.length = 0;
            free.length = 0;
            for (var i:int = 0; i < head.length; i++) head[i] = -1;
        }

    }
}
