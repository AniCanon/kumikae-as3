package dev.kumikae.lanternreach.sim {
    public final class CreaturePool {
        public var slots:Vector.<Creature> = new Vector.<Creature>();
        private var free:Vector.<int> = new Vector.<int>();
        public var count:int;
        public var peak:int;

        public function spawn(x:Number, y:Number):Creature {
            var slot:int = slots.length;
            if (free.length > 0) {
                slot = free.pop();
            } else {
                slots.push(new Creature());
            }
            var creature:Creature = slots[slot];
            creature.reset(slot, x, y);
            count++;
            if (count > peak) peak = count;
            return creature;
        }

        public function release(creature:Creature):void {
            if (!creature.active) return;
            creature.active = false;
            free.push(creature.id);
            count--;
        }
        public function clear():void {
            slots.length = 0;
            free.length = 0;
            count = 0;
        }

    }
}
