package dev.kumikae.lanternreach.sim {
    public final class Shot {
        public var active:Boolean;
        public var x:Number;
        public var y:Number;
        public var dx:Number;
        public var dy:Number;
        public var left:Number;
        public var damage:int;
        public var remaining:int;
        public var hit:Vector.<int> = new Vector.<int>();
    }
}
