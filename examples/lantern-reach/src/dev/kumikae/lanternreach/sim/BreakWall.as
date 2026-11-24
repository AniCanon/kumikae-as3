package dev.kumikae.lanternreach.sim {
    public final class BreakWall implements IEnemyState {
        public function think(actor:Creature, senses:EnemySenses):void {
            var beacon:int = senses.beacon(actor);
            if (beacon < 0) return;
            var dx:Number = beacon % senses.grid.width + 0.5 - actor.x;
            var dy:Number = int(beacon / senses.grid.width) + 0.5 - actor.y;
            var scale:Number = Math.max(1, Math.max(Math.abs(dx), Math.abs(dy)));
            for (var step:int = 1; step <= 4; step++) {
                var id:int = senses.grid.cell(actor.x + dx * step / scale, actor.y + dy * step / scale);
                if (senses.grid.walls[id]) {
                    senses.seek(actor, id);
                    senses.strike(actor, senses.town.at[id]);
                    return;
                }
            }
        }
    }
}
