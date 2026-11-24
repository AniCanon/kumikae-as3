package dev.kumikae.lanternreach.sim {
    public final class EventBus {
        private var listeners:Vector.<Subscription> = new Vector.<Subscription>();
        private var pending:Vector.<WorldEvent> = new Vector.<WorldEvent>();

        public function listen(callback:(WorldEvent)=>void):Subscription {
            var slot:Subscription = new Subscription();
            slot.notify = callback;
            listeners.push(slot);
            return slot;
        }

        public function remove(slot:Subscription):void {
            slot.active = false;
            slot.notify = null;
            var i:int = listeners.indexOf(slot);
            if (i >= 0) listeners.splice(i, 1);
        }

        public function post(kind:int, cell:int, value:int):void {
            pending.push(new WorldEvent(kind, cell, value));
        }

        public function dispatch():void {
            if (pending.length == 0) return;
            var snapshot:Vector.<Subscription> = listeners.slice();
            for each (var event:WorldEvent in pending) {
                for each (var slot:Subscription in snapshot) {
                    if (slot.active) slot.notify(event);
                }
            }
            pending.length = 0;
        }
    }
}
