package dev.kumikae.lanternreach.sim {
    public final class TickCommand {
        public var tick:int;
        public var action:String;
        public var x:Number;
        public var y:Number;

        public function TickCommand(at:int, verb:String, a:Number, b:Number) {
            tick = at;
            action = verb;
            x = a;
            y = b;
        }
    }
}
