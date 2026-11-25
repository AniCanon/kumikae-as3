package dev.kumikae.lanternreach.sim {
    public final class ShotPool {
        public var shots:Vector.<Shot> = new Vector.<Shot>();
        private var free:Vector.<Shot> = new Vector.<Shot>();

        public function fire(x:Number, y:Number, dx:Number, dy:Number, damage:int, reach:int, pierce:int):void {
            var length:Number = Math.sqrt(dx * dx + dy * dy);
            if (length == 0) return;
            var shot:Shot;
            if (free.length == 0) {
                shot = new Shot();
                shots.push(shot);
            } else {
                shot = free.pop();
            }
            shot.x = x;
            shot.y = y;
            shot.dx = dx / length;
            shot.dy = dy / length;
            shot.left = reach;
            shot.damage = damage;
            shot.remaining = pierce;
            shot.active = true;
            shot.hit.length = 0;
        }

        public function release(shot:Shot):void {
            shot.active = false;
            free.push(shot);
        }
        public function clear():void {
            shots.length = 0;
            free.length = 0;
        }

    }
}
