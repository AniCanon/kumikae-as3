package dev.kumikae.lanternreach.sim {
    public final class Subscription {
        public var active:Boolean = true;
        public var notify:(WorldEvent)=>void;
    }
}
